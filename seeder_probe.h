#ifndef __SEEDER_PROBE_H
#define __SEEDER_PROBE_H

#include "../inc/network_file.h"
#include <optional>
#include <vector>

struct SeederProbeResult {
    optional<Seeder> first_live; // start the download
    vector<Seeder> reachable;    // keep all reachable seeders
};

struct ProbeOptions {
    bool verify_file = true;
    int timeout_ms = 800;
};

class SeederProbe {
  public:
    SeederProbeResult probe(const network_file &nf, const ProbeOptions &opt = {});
};

#endif
