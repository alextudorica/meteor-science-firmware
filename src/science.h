#pragma once

// Science mode: NR-free, all-intra H.264 with a per-frame RMSP v5 provenance SEI,
// and an isp_ctl-compatible control server on TCP 9600 (the protocol RMS alpha2 speaks).

#include <stddef.h>

#ifdef SCIENCE
// Start the maintenance thread (ISP invariants, sun-driven day/night) and the :9600 server.
void science_start(void);
// Apply encoder invariants (deblocking off) to a freshly created H.264 channel.
void science_venc_created(int chn);
// Build an Annex-B SEI NAL (start code included) describing the frame with this PTS.
// Returns its length, or 0 if it could not be built.
int science_sei(unsigned char *out, size_t max, unsigned long long ptsUs,
    unsigned int seq, int qp, int startQp);
#endif
