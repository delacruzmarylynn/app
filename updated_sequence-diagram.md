
sequenceDiagram
autonumber
actor User
participant Main as main() / UI loop
participant App as seed_app
participant PM as port_manager
participant Host as Host
participant Tick as tick_thread (pthread)
participant Srv as server_thread (pthread)
participant DM as download_manager
participant Res as AppSeederResolver
participant SP as SeederProbe
participant C as Client
participant DL as Downloader

Note over Main: Program start
Main->>App: seed_app app;
Main->>App: app.start()
App->>PM: reserve_port(9000..9004 loop)
alt port reserved
  PM-->>App: fd != -1, chosen_port set
  App->>DM: dm.emplace(chosen_port)
else no port available
  PM-->>App: fd == -1
end
Main->>Main: signal(SIGPIPE, SIG_IGN)
Main->>App: fd = app.get_fd()
Main->>App: port = app.get_port()

alt fd != -1
  Main->>Host: Host host(fd, port)
  Main->>Srv: pthread_create(server_thread, seed_app::server_entry, &host)
  Note over Srv: server_entry(host) calls host.accept_clients()
  Srv->>Host: accept_clients() loop (listen/accept)
  
  Main->>Tick: pthread_create(tick_thread, tick_entry, TickArg{&app,&tick_running})
  Note over Tick: tick_entry() loops every 2s calling app.tick_downloads()
  
  loop menu loop while is_running
    Main->>App: display_menu()
    User->>Main: enter choice
    
    alt choice == 1 (Download file)
      Note over Main: Create resolver + discover available files
      Main->>Res: make_shared<AppSeederResolver>(app, peers, port)
      Main->>App: set_seeder_resolver(resolver)
      App->>DM: set_seeder_resolver(resolver)
      
      Main->>App: list_unique_files(peers, port)
      loop for each peer p != port
        App->>C: Client(p)
        App->>C: send_choice(1)
        App->>C: receive_response()  (file list)
        App->>C: close_connection()
      end
      App-->>Main: vector<network_file> available_files
      
      User->>Main: pick file_id
      Main->>Res: resolve(snapshot.key())
      Res->>App: list_unique_files(peers, my_port)
      Res->>App: find_by_key(files, file_key)
      alt found network_file
        Res->>SP: probe(*nf, opt{verify_file=true})
        loop for each candidate seeder in nf.seeders
          SP->>C: Client(seeder.port)
          SP->>C: send_choice(3)
          SP->>C: send_have_request(name,size)
          SP->>C: receive_response() (YES/NO)
        end
        SP-->>Res: reachable seeders
      else not found
        Res-->>Main: empty seeder list
      end
      
      alt live_seeders not empty
        Main->>App: start_download(snapshot, live_seeders)
        App->>DM: add_download(snapshot, live_seeders)
        Note over DM: add_download creates download job + spawns worker threads
        loop for each Seeder s in seeders
          DM->>DM: spawn_worker_for(job, s)
          DM->>DM: pthread_create(worker_entry, WorkerArg{self, job, s})
        end
      else no seeders
        Main-->>User: "No seeders found"
      end
      
    else choice == 2 (Download status screen)
      Note over Main: Repeatedly display status until key press
      loop until key pressed
        Main->>App: has_downloads()
        alt has downloads
          Main->>App: download_status()
          App->>DM: get_status_string()
          DM-->>App: formatted status lines
          App-->>Main: status string
        else none
          App-->>Main: "No active downloads"
        end
      end
      
    else choice == 3 (Exit)
      Main->>Main: is_running = false
    else invalid
      Main-->>User: "Invalid input"
    end
  end
  
  Note over Main: Shutdown sequence
  Main->>Tick: tick_running = false
  Main->>Tick: pthread_join(tick_thread)
  Main->>Host: host.stop_server()
  Main->>Srv: pthread_join(server_thread)
  Main->>App: app.exit()
  App->>PM: release_port(chosen_port)
  App->>DM: dm.reset()
else fd == -1
  Note over Main: Server not started (no reserved port)
end

%% Background worker behavior (happens concurrently after download starts)
par Worker threads (download_manager::worker_run)
  DM->>C: Client(seeder.port) (one connection per worker)
  loop claim chunks until none
    DM->>DL: fetch_chunk_on_connection(c, file_key, offset, want)
    DL->>C: send_choice(2) + send key/offset/len
    Note over Host: server_thread handles client choice 2
    Host->>Host: handle_get_chunk() read request, read file bytes
    Host-->>C: send_chunk_response(status,data/err)
    C-->>DL: recv response
    DL-->>DM: ChunkResult(ok,data/error)
    DM->>DM: write chunk to .part (locked) + mark chunk done
  end
end
