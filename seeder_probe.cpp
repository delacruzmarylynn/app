#include "../inc/seeder_probe.h"
#include "../inc/Client.h"

// actual network connection

// seed_app = discovery + indexing
// network_file = metadata + candidate seeders
// SeederProbe = connectivity + validation
// SeederProbe decides what to ask
SeederProbeResult SeederProbe::probe(const network_file &nf, const ProbeOptions &opt) {
    SeederProbeResult result;

    const auto &info = nf.get_info();
    const auto &candidates = nf.get_seeders();

    for (const auto &s : candidates) {
        try {
            Client c(s.port);
            c.send_choice(4);

            if (opt.verify_file) {
                // ask if seeder has THIS file
                // e.g. HAVE\tname\tsize
                c.send_have_request(info.name, info.size);

                std::string reply = c.receive_response();

                if (reply.rfind("YES", 0) != 0) {
                    continue;
                }
            }

            result.reachable.push_back(s);
            if (!result.first_live.has_value()) {
                result.first_live = s;
            }

        } catch (...) {
        }
    }

    return result;
}
