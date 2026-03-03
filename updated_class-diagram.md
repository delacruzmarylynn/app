classDiagram
direction LR

%% =========================
%% Core domain
%% =========================

class File {
  +int id
  +string name
  +uint64_t size
  +File(int id_, string name_, uint64_t size_)
  +string key() const
}

class Seeder {
  +uint16_t port
  +Seeder(uint16_t p)
}

class network_file {
  -File info
  -vector~Seeder~ seeders
  +const File& get_info() const
  +const vector~Seeder~& get_seeders() const
  +bool has_seeders() const
  +string key() const
  +void set_id(int id_)
  +void add_seeder(Seeder s)
}

%% =========================
%% Networking
%% =========================

class Client {
  +Client(int port)
  +bool send_choice(int choice)
  +bool send_all(const void* data, size_t len)
  +bool recv_exact(void* data, size_t len)
  +void send_string(string msg)
  +void send_have_request(string name, uint64_t size)
  +string receive_response()
  +void close_connection()
  +~Client()
}

class Host {
  +Host(int socket_fd, uint16_t port_num)
  +void accept_clients()
  +int connect_to_peer(int peer_port)
  +void stop_server()
  +const vector~int~& connected_ports() const
  +bool send_response(int client_socket, string reply)
  +bool handle_get_chunk(int client_socket)
  +string list_available_files() const
}

class PeerThread  {
  <<struct>>
  +Host* host
  +int fd
  +int peer_port
}

class ClientThread  {
  <<struct>>
  +Host* host
  +int client_socket
}

%% =========================
%% Resolver + Probe
%% =========================

class SeederResolver {
  <<interface>> 
  +vector~Seeder~ resolve(string file_key)*
}

class AppSeederResolver {
  +AppSeederResolver(seed_app& app, vector~int~ peers, uint16_t my_port)
  +vector~Seeder~ resolve(string file_key)
}

class SeederProbe {
  +SeederProbeResult probe(network_file nf, ProbeOptions opt)
}

class SeederProbeResult  {
  <<struct>>
  +optional~Seeder~ first_live
  +vector~Seeder~ reachable
}

class ProbeOptions  {
  <<struct>>
  +bool verify_file
  +int timeout_ms
}

%% =========================
%% Downloading
%% =========================

class ChunkResult  {
  <<struct>>
  +bool ok
  +vector~uint8_t~ data
  +string error
}

class Downloader {
  +GET_CHUNK_CHOICE : int
  +ChunkResult fetch_chunk_on_connection(Client& c, string file_key, uint64_t offset, uint32_t length)
}

class DownloadStatus  {
  <<enumeration>>
  Idle
  Downloading
  Completed
  Stalled
  Failed
  Cancelled
}

class download  {
  <<struct>>
  +string file_key
  +int file_id
  +string file_name
  +uint64_t total_bytes
  +atomic~DownloadStatus~ state
  +vector~Seeder~ seeders
  +vector~pthread_t~ workers
  +atomic~bool~ cancel_requested
  +string output_path
  +string final_path
  +string error
}

class download_manager {
  +download_manager(uint16_t port)
  +void set_seeder_resolver(shared_ptr~SeederResolver~ r)
  +void tick()
  +bool add_download(network_file nf, vector~Seeder~ seeders)
  +void cancel(string file_key)
  +string get_status_string() const
  +bool all_downloads_completed() const
  +bool has_active_downloads() const
  +bool has_downloading() const
}

class WorkerArg  {
  <<struct>>
  +download_manager* self
  +shared_ptr~download~ job
  +Seeder seeder
}

%% =========================
%% App + ports
%% =========================

class port_manager {
  +port_manager(uint16_t base_port=9000, size_t count=5)
  +int reserve_port(uint16_t port)
  +void release_port(uint16_t port)
}

class seed_app {
  +seed_app()
  +void start()
  +int get_fd() const
  +int get_port() const
  +void display_menu()
  +static void* server_entry(void* arg)
  +void set_seeder_resolver(shared_ptr~SeederResolver~ r)
  +static vector~network_file~ list_unique_files(vector~int~ peers, int port)
  +static const network_file* find_by_key(vector~network_file~ files, string key)
  +void tick_downloads()
  +bool start_download(network_file nf, vector~Seeder~ seeders)
  +string download_status() const
  +bool downloads_completed() const
  +bool has_downloads() const
  +void exit()
}

%% =========================
%% Relationships + multiplicity
%% =========================

network_file "1" *-- "1" File : info
network_file "1" *-- "0..*" Seeder : seeders

SeederResolver <|.. AppSeederResolver
AppSeederResolver "1" --> "1" seed_app : app_
AppSeederResolver ..> SeederProbe : uses

SeederProbe ..> Client : uses
SeederProbe ..> network_file : probes
SeederProbe "1" --> "1" SeederProbeResult : returns
SeederProbeResult "1" o-- "0..1" Seeder : first_live
SeederProbeResult "1" o-- "0..*" Seeder : reachable

Host "1" o-- "0..*" ClientThread : creates
Host "1" o-- "0..*" PeerThread : creates
ClientThread "0..1" --> "1" Host : host
PeerThread "0..1" --> "1" Host : host

seed_app "1" *-- "1" port_manager : pm
seed_app "1" o-- "0..1" download_manager : dm (optional)

%% Behavior-level dependencies (from .cpp)
seed_app ..> Client : uses (list_unique_files)
seed_app ..> Host : uses (server thread)

download_manager "1" o-- "0..*" download : jobs
download_manager "1" o-- "0..1" SeederResolver : resolver
download_manager "1" o-- "0..*" WorkerArg : spawns

WorkerArg "1" --> "1" download_manager : self
WorkerArg "1" --> "1" download : job
WorkerArg "1" --> "1" Seeder : seeder

download_manager ..> Downloader : uses
download_manager ..> Client : uses (per-seeder worker connection)
Downloader ..> Client : uses
Downloader "1" --> "1" ChunkResult : returns