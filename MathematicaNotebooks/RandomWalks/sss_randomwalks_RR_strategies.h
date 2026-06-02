
/*
 * On Russian Roulette (RR) as a termination strategy for low energy paths
 * with multiple sampling strategies .......................................
 *
 * Path survives to RR with probability S.
 * If it survives, the contribution is normalized by S (keeps the estimator unbiased).
 *
 * But what if we're using Dwivedi guided sampling that does not use RR 
 * (because it introduces variance)?
 *
 * Look at the MIS balance heuristic for two strategies :
 * A (Guided, S_A=1) and B (Classic, S_B < 1):
 *
 * Weight_A = (PDF_A * S_A) / [ (PDF_A * S_A) + (PDF_B * S_B) ]
 *
 * Because (PDF_B * S) is smaller than (PDF_B), the MIS weight for 
 * the Guided strategy (A) becomes larger.
 *
 * The MIS integrator is now "RR-Aware": tt gives more credit to the strategies 
 * that don't get terminated by RR. This reduces variance in deep-scattering.
 */

// --- 1. STANDARD RR (Post-hoc weight compensation) ---
// The MIS weighting is unaware of RR survival probability 'S'.
// S is applied as an after-the-fact correction factor (compensation).

// Per-bounce MIS: balance heuristic over RGB channel PDFs
throughput *= transmittance / dot(channel_pdf, pdf);

if (!guided_sampling) {
    const float survival_prob = min(max3(fabs(throughput)), 1.0f);
    const float dice = Sampling::CMJ::cmj_sample_1D(..., PRNG_PHASE_CHANNEL);

    if (dice >= survival_prob) break; // Path terminated
    
    // Weight compensation applied to the estimator (to keep it unbiased)
    throughput /= survival_prob; 
}


// --- 2. RR-AWARE MIS (Unified Estimator) ---
// The survival probability 'S' is integrated into the MIS denominator.
// MIS recognizes that surviving paths are effectively rarer.

if (!guided_sampling) {
    const float survival_prob = min(max3(fabs(throughput)), 1.0f);
    const float dice = Sampling::CMJ::cmj_sample_1D(..., PRNG_PHASE_CHANNEL);

    if (dice >= survival_prob) break; // Path terminated

    // S is NOT applied here; it is integrated directly into the MIS update below.
    // We treat (combined_pdf * survival_prob) as the total PDF of the event.
    throughput *= transmittance / (dot(channel_pdf, combined_pdf) * survival_prob);
} else {
    // If no RR (or guided path), survival_prob is effectively 1.0.
    throughput *= transmittance / dot(channel_pdf, combined_pdf);
}

// In 2. - we're not compensating a terminated estimator (as in 1.)
// but we're computing the probability of the joint event: 
// "path reached this bounce AND survived RR." (ie. pdf * S)
// We compute the PDF of the scatteringg event given that it survived the RR.
// Ie. RR as a pure termination strategy vs RR as a sampling weight.
