#pragma once

namespace xray::render::RENDER_NAMESPACE
{
// DA: temporal anti-aliasing resolve (R4 only). Blends the previous frame into the current one after
// reprojecting through depth — see gamedata\shaders\r3\da_taa.ps for the reasoning.
class CBlender_TAA : public IBlender
{
public:
    virtual LPCSTR getComment() { return "INTERNAL: DA temporal AA resolve"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }
    virtual void Compile(CBlender_Compile& C);

    CBlender_TAA();
    virtual ~CBlender_TAA();
};
} // namespace xray::render::RENDER_NAMESPACE
