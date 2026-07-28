#ifndef DA_SHADER_IDS_H
#define DA_SHADER_IDS_H

// [DA_PORT] Debug palette: every shader that fills the G-buffer stamps a fixed pair into the velocity
// target instead of a real motion vector, so "r__motion_vectors 3" paints the world by WHICH SHADER
// drew each pixel. Chasing this by shader names in the log cost a whole evening — seeing it directly
// takes one screenshot.
//
// The debug view maps the pair to colour as
//     red = saturate(x), green = saturate(-x), blue = saturate(abs(y))
// so the pairs below are chosen to come out as clearly distinct colours. Values are large enough to
// saturate at the debug multiplier, i.e. every stamp shows at full brightness.
//
// Mapping (keep this table in sync — it is the legend for the screenshot):
//     RED       deffer_base_bump        walls, crates, most world geometry with normal maps
//     GREEN     deffer_base_flat        world geometry without normal maps
//     BLUE      deffer_base_aref_bump   alpha-tested with normal maps: bushes, fences, gear
//     MAGENTA   deffer_base_aref_flat   alpha-tested without: grass cards, foliage
//     CYAN      deffer_base             the mod's own base shader (vegetation soft edges live here)
//     YELLOW    deffer_impl_flat        impostors — the flat stand-ins for distant objects
//     DIM PURPLE     anything else reaching pack_gbuffer without its own stamp
#define DA_ID_BUMP        float2( 0.05,  0.00)
#define DA_ID_FLAT        float2(-0.05,  0.00)
#define DA_ID_AREF_BUMP   float2( 0.00,  0.05)
#define DA_ID_AREF_FLAT   float2( 0.05,  0.05)
#define DA_ID_BASE        float2(-0.05,  0.05)
#define DA_ID_IMPL        float2( 0.05,  0.02)
#define DA_ID_UNKNOWN     float2( 0.02,  0.02)

#endif
