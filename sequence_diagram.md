```mermaid
sequenceDiagram
    autonumber

    participant Main as Main Thread (seed_app.cpp: main)
    participant App as seed_app
    participant PM as port_manager
    participant DM as download_manager
    participant Host as Host
    participant ServerT as server_thread (pthread)
    participant TickT as tick_thread (pthread)
    participant Resolver as AppSeederResolver (SeederResolver)
    participant Probe as SeederProbe
    participant Peer as Peer Nodes (ports 9000-9004)
    participant Worker as Download Worker Thread(s)
    participant Client as Client (TCP)

    %% --- Startup ---
    Main->>App: app.start()
    App->>PM: reserve_port(9000..9004)
    PM-->>App: chosen_port + fd
    App->>DM: dm.emplace(chosen_port)

    Main->>Host: Host(fd, port)
    Main->>ServerT: pthread_create(server_entry, &Host)
    ServerT->>Host: accept_clients()
    note over Host,ServerT: accept() loop
    note over Host,ServerT: for each inbound connection
    Host-->>Host: pthread_create(handle_client_entry) per client (detached)

    Main->>TickT: pthread_create(tick_entry, &app)
    loop every 2s while running
        TickT->>App: tick_downloads()
        App->>DM: dm.tick()
        alt job is Stalled/Downloading and due for refresh
            DM->>DM: refresh_seeders(job)
            DM->>Resolver: resolve(job.file_key)
            Resolver->>App: list_unique_files(peers, my_port)
            App->>Peer: for each peer: Client(peer_port).send_choice(1)
            Peer-->>App: file list (name\\tsize...)
            Resolver->>App: find_by_key(files, file_key)
            Resolver->>Probe: probe(network_file, verify_file=true)
            Probe->>Peer: for each candidate seeder: Client(seeder.port).send_choice(3)
            Probe->>Peer: send_have_request(name,size)
            Peer-->>Probe: YES/NO
            Probe-->>Resolver: reachable seeders
            Resolver-->>DM: live seeders
            DM->>DM: spawn_worker_for(new seeder)
            DM->>Worker: pthread_create(worker_entry)
        end
    end

    %% --- User actions via menu (main thread) ---
    loop Menu
        Main->>App: display_menu() / read choice

        alt Choice 1: Download file
            Main->>Resolver: make_shared(AppSeederResolver(app, peers, port))
            Main->>App: set_seeder_resolver(resolver)
            App->>DM: set_seeder_resolver(resolver)

            Main->>App: list_unique_files(peers, port)
            App->>Peer: Client(peer_port).send_choice(1)
            Peer-->>App: list of files
            Main->>Resolver: resolve(selected.file_key)
            Resolver->>Probe: probe(... verify_file=true)
            Probe-->>Resolver: reachable seeders

            Main->>App: start_download(network_file, live_seeders)
            App->>DM: add_download(nf, seeders)
            DM->>DM: create/prepare .part file
            DM->>DM: init chunk_state + counters
            loop for each seeder
                DM->>Worker: pthread_create(worker_entry)
            end

            note over Worker: Each worker keeps one TCP connection to its seeder
            Worker->>Client: Client(seeder.port) connect()
            loop claim chunks
                Worker->>DM: claim_chunk_static() (CAS 0->1)
                Worker->>Client: downloader.fetch_chunk_on_connection(key, offset, len)
                Client-->>Worker: chunk bytes
                Worker->>DM: write to .part (job.io_mtx)
                Worker->>DM: mark chunk_state=2
                note over DM,Worker: increment chunks_done
                note over DM,Worker: increment downloaded_bytes
            end

        else Choice 2: Status screen
            Main->>App: download_status()
            App->>DM: get_status_string()
            DM-->>Main: formatted status

        else Choice 3: Exit
            Main->>TickT: tick_running=false
            note over TickT,Main: pthread_join
            Main->>Host: stop_server() (shutdown+close)
            Main->>ServerT: pthread_join
            Main->>App: exit() (pm.release_port, dm.reset)
        end
    end