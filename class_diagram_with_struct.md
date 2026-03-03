classDiagram
direction LR

class File {
  +int id
  +string name
  +uint64_t size
  +File()
  +File(int id_, string name_, uint64_t size_)
  +string key() const
}  

class Seeder {
  +uint16_t port
  +Seeder()
  +Seeder(uint16_t p)
} 

class network_file {
  -File info
  -vector~Seeder~ seeders
  +network_file()
  +network_file(File fi)
  +const File& get_info() const
  +const vector~Seeder~& get_seeders() const
  +bool has_seeders() const
  +string key() const
  +void set_id(int id_)
  +void add_seeder(Seeder s)
} 

class Client {
  -int client_fd
  -int client_id
  -static bool recv_all(int client_socket, void* data, size_t len)
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
  -int fd
  -uint16_t port
  -vector~int~ connected_
  -bool is_running
  -static void* handle_peer_entry(void* arg)
  -void handle_peer(int peer_fd)
  -static void* handle_client_entry(void* arg)
  -void handle_client(int client_socket)
  -bool has_file(string name, uint64_t size) const
  +Host(int socket_fd, uint16_t port_num)
  +void accept_clients()
  +int connect_to_peer(int peer_port)
  +void stop_server()
  +const vector~int~& connected_ports() const
  +static bool send_all(int client_socket, const void* data, size_t len)
  +static bool recv_all(int client_socket, void* data, size_t len)
  +bool send_response(int client_socket, string reply)
  +static bool recv_string(Host* h, int sock, string& out)
  +bool send_chunk_response(int client_socket, int32_t status, vector~uint8_t~ data, string err)
  +bool handle_get_chunk(int client_socket)
  +vector~int~ connect_to_peers(initializer_list~int~ peers)
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


class SeederResolver  {
<<interface>>
  +~SeederResolver()
  +vector~Seeder~ resolve(string file_key)*
}

class AppSeederResolver {
  -seed_app& app_
  -vector~int~ peers_
  -uint16_t my_port_
  +AppSeederResolver(seed_app& app, vector~int~ peers, uint16_t my_port)
  +vector~Seeder~ resolve(string file_key)
}

class SeederProbe {
  +SeederProbeResult probe(network_file nf, ProbeOptions opt)
}

class SeederProbeResult {
<<struct>> 
  +optional~Seeder~ first_live
  +vector~Seeder~ reachable
} 

class ProbeOptions  {
<<struct>>
  +bool verify_file
  +int timeout_ms
} 


class Downloader {
  +GET_CHUNK_CHOICE : int
  +ChunkResult fetch_chunk_on_connection(Client& c, string file_key, uint64_t offset, uint32_t length)
  -static uint64_t htonll(uint64_t x)
  -static uint64_t ntohll(uint64_t x)
  -static bool send_i32(Client& c, int32_t v)
  -static bool send_u32(Client& c, uint32_t v)
  -static bool send_u64(Client& c, uint64_t v)
  -static bool send_string(Client& c, string s)
  -static bool recv_i32(Client& c, int32_t& out)
  -static bool recv_u32(Client& c, uint32_t& out)
  -static bool recv_string(Client& c, string& out)
} 

class ChunkResult  {
<<struct>>
  +bool ok
  +vector~uint8_t~ data
  +string error
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
  +atomic~uint64_t~ downloaded_bytes
  +atomic~DownloadStatus~ state
  +vector~Seeder~ seeders
  +atomic~uint64_t~ next_offset
  +atomic~bool~ failed
  +atomic~int~ active_workers
  +atomic~bool~ refreshing
  +atomic~time_t~ last_resolve
  +uint32_t chunk_size
  +uint64_t total_chunks
  +deque~atomic~uint8_t~~ chunk_state
  +atomic~uint64_t~ chunks_done
  +atomic~int~ usable_seeder_workers
  +string output_path
  +string final_path
  +string error
  +vector~pthread_t~ workers
  +atomic~bool~ cancel_requested
  +atomic~bool~ worker_running
  +pthread_mutex_t m
  +pthread_mutex_t io_mtx
  +pthread_mutex_t seeders_m
  +unordered_set~uint16_t~ seeder_ports
} 

class download_manager {
  -uint16_t my_port
  -pthread_mutex_t jobs_mtx
  -unordered_map~string, shared_ptr~download~~ jobs
  -shared_ptr~SeederResolver~ resolver
  -static void* worker_entry(void* arg)
  -void worker_run(shared_ptr~download~ job, Seeder seeder)
  -static void ensure_dir(string path)
  -static bool file_exists(string path)
  -uint64_t file_size(string path)
  -shared_ptr~download~ find_job_by_key_locked(string key) const
  -bool spawn_worker_for(shared_ptr~download~ job, Seeder s)
  -void mark_seeder_dead(shared_ptr~download~ job, uint16_t port)
  -void refresh_seeders(shared_ptr~download~ job)
  +download_manager(uint16_t port)
  +~download_manager()
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

class port_manager {
  -vector~uint16_t~ ports
  -vector~bool~ used_ports
  -sockaddr_in address
  -int reserved_fd
  +port_manager(uint16_t base_port=9000, size_t count=5)
  +int reserve_port(uint16_t port)
  +void release_port(uint16_t port)
} 

class seed_app {
  -atomic~bool~ is_running
  -port_manager pm
  -network_file nf
  -int fd
  -uint16_t chosen_port
  -optional~download_manager~ dm
  -unique_ptr~AppSeederResolver~ resolver_
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
  +~seed_app()
}

%% network_file composition
network_file "1" *-- "1" File : info
network_file "1" *-- "0..*" Seeder : seeders

%% Host thread-arg structs
Host "1" o-- "0..*" PeerThread : args
Host "1" o-- "0..*" ClientThread : args

%% Downloader depends on Client
Downloader ..> Client : uses connection
Downloader "1" --> "1" ChunkResult : returns

%% SeederResolver inheritance
SeederResolver <|.. AppSeederResolver

%% SeederProbe dependencies
SeederProbe ..> Client : uses (includes)
SeederProbe ..> network_file : probes
SeederProbe "1" --> "1" SeederProbeResult : returns
SeederProbeResult "1" o-- "0..1" Seeder : first_live
SeederProbeResult "1" o-- "0..*" Seeder : reachable
SeederProbe ..> ProbeOptions : options

%% seed_app owns port manager and holds optional download_manager + resolver
seed_app "1" *-- "1" port_manager : pm
seed_app "1" *-- "1" network_file : nf
seed_app "1" o-- "0..1" download_manager : dm (optional)
seed_app "1" o-- "0..1" AppSeederResolver : resolver_ (unique_ptr)

%% AppSeederResolver uses seed_app + probing/resolving
AppSeederResolver "1" --> "1" seed_app : app_
AppSeederResolver ..> SeederProbe : uses
AppSeederResolver ..> SeederResolver : implements

%% download_manager manages many downloads and uses resolver
download_manager "1" o-- "0..*" download : jobs 
download_manager "1" o-- "0..1" SeederResolver : resolver (shared_ptr)

%% download contains many seeders and workers
download "1" o-- "0..*" Seeder : seeders
download "1" o-- "0..*" WorkerArg : wa 
WorkerArg "1" --> "1" download_manager : self
WorkerArg "1" --> "1" download : job
WorkerArg "1" --> "1" Seeder : seeder

%% download_manager uses network_file input and spawns workers that use Downloader/Client
download_manager ..> network_file : add_download(nf)
download_manager ..> Downloader : worker_run uses
download_manager ..> Client : worker threads connect

%% seed_app delegates download management
seed_app ..> download_manager : tick/start_download
seed_app ..> SeederResolver : set_seeder_resolver