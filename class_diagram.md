```mermaid
classDiagram
direction LR

class File {
  +int id
  +string name
  +uint64_t size
  +string key() const
}

class Seeder {
  +uint16_t port
}

class network_file {
  -File info
  -vector~Seeder~ seeders
  +const File& get_info() const
  +const vector~Seeder~& get_seeders() const
  +bool has_seeders() const
  +string key() const
  +void set_id(int)
  +void add_seeder(Seeder)
}

class SeederResolver {
  <<interface>>
  +vector~Seeder~ resolve(string file_key)*
}

class AppSeederResolver {
  -seed_app& app_
  -vector~int~ peers_
  -uint16_t my_port_
  +vector~Seeder~ resolve(string file_key)
}

class SeederProbe {
  +SeederProbeResult probe(network_file nf, ProbeOptions opt)
}

class SeederProbeResult {
  +optional~Seeder~ first_live
  +vector~Seeder~ reachable
}

class ProbeOptions {
  +bool verify_file
  +int timeout_ms
}

class Client {
  -int client_fd
  -int client_id
  +Client(int port)
  +bool send_choice(int)
  +bool send_all(void*, size_t)
  +bool recv_exact(void*, size_t)
  +void send_string(string)
  +void send_have_request(string, uint64_t)
  +string receive_response()
  +void close_connection()
}

class Downloader {
  +ChunkResult fetch_chunk_on_connection(Client c, string file_key, uint64_t offset, uint32_t length)
}

class ChunkResult {
  +bool ok
  +vector~uint8_t~ data
  +string error
}

class download_manager {
  -uint16_t my_port
  -unordered_map~string, shared_ptr~download~~ jobs
  -shared_ptr~SeederResolver~ resolver
  +void set_seeder_resolver(shared_ptr~SeederResolver~)
  +void tick()
  +bool add_download(network_file nf, vector~Seeder~ seeders)
  +void cancel(string file_key)
  +string get_status_string() const
  +bool all_downloads_completed() const
  +bool has_active_downloads() const
  +bool has_downloading() const
}

class download {
  +string file_key
  +string file_name
  +uint64_t total_bytes
  +atomic~uint64_t~ downloaded_bytes
  +atomic~DownloadStatus~ state
  +vector~Seeder~ seeders
  +atomic~uint64_t~ next_offset
  +uint32_t chunk_size
  +vector~pthread_t~ workers
  +string output_path
  +string final_path
  +string error
}

class port_manager {
  -vector~uint16_t~ ports
  -vector~bool~ used_ports
  +int reserve_port(uint16_t)
  +void release_port(uint16_t)
}

class seed_app {
  -port_manager pm
  -network_file nf
  -optional~download_manager~ dm
  -unique_ptr~AppSeederResolver~ resolver_
  +void start()
  +void set_seeder_resolver(shared_ptr~SeederResolver~)
  +void tick_downloads()
  +bool start_download(network_file nf, vector~Seeder~ seeders)
  +string download_status() const
  +bool downloads_completed() const
  +bool has_downloads() const
  +void exit()
}

class Host {
  -int fd
  -uint16_t port
  -vector~int~ connected_
  +void accept_clients()
  +int connect_to_peer(int peer_port)
  +vector~int~ connect_to_peers(initializer_list~int~ peers)
  +void stop_server()
  +const vector~int~& connected_ports() const
}

%% =========================
%% Relationships + Multiplicity
%% =========================

network_file "1" *-- "1" File : info
network_file "1" *-- "0..*" Seeder : seeders

SeederResolver <|-- AppSeederResolver
AppSeederResolver "1" --> "1" seed_app : app_
AppSeederResolver ..> SeederProbe : uses

SeederProbe ..> Client : uses
SeederProbe ..> network_file : probes
SeederProbeResult "1" o-- "0..1" Seeder : first_live
SeederProbeResult "1" o-- "0..*" Seeder : reachable

Downloader ..> Client : uses
Downloader --> ChunkResult : returns

seed_app "1" *-- "1" port_manager : pm
seed_app "1" *-- "1" network_file : nf
seed_app "1" o-- "0..1" download_manager : dm (optional)
seed_app "1" o-- "0..1" AppSeederResolver : resolver_ (unique_ptr)
seed_app ..> SeederResolver : sets resolver

download_manager "1" o-- "0..1" SeederResolver : resolver (shared_ptr)
download_manager "1" o-- "0..*" download : jobs (shared_ptr)
download "1" o-- "0..*" Seeder : seeders

download_manager ..> network_file : add_download()
download_manager ..> Downloader : uses

Host "1" o-- "0..*" int : connected_ports
seed_app ..> Host : uses