#include "Gradient.h"
#include "GlobalDefs.h"
#include <cstdio>
#include <set>
using namespace monie;
int main(){ int f=0; auto C=[&](bool c,const char*m){ if(!c){++f;printf("FAIL: ");} else printf("ok:   "); printf("%s\n",m); };
 std::set<std::string> ids; bool ok=true; for(auto&d:globalDefs()){ if(!ids.insert(d.id).second||d.maxV<=d.minV||d.defV<d.minV||d.defV>d.maxV) ok=false; }
 C(ok && globalDefs().size()>=30,"global defs unique & valid & >=30");
 Gradient g=builtinPalettes()[1]; Gradient h; C(Gradient::fromText(g.toText(),h)&&h.stops.size()==g.stops.size()&&toHex(h.stops[1].c)==toHex(g.stops[1].c),"gradient text round-trip");
 C(!Gradient::fromText("x|0:#zzz,1:#fff",h)&&!Gradient::fromText("nope",h),"bad gradient text rejected");
 unsigned char lut[256*4]; g.bake(lut); C(lut[3]==255 && lut[0]!=lut[255*4],"bake LUT");
 Gradient q; C(Gradient::fromHexList("#ff0000\n00ff00 0000ff\n",  "t", q) && q.stops.size()==3,"hex list import"); 
 Gradient gp; C(Gradient::fromHexList("GIMP Palette\nName: x\n255 0 0 red\n0 255 0 green\n",  "t", gp) && gp.stops.size()==2,"gpl-style import");
 float hh,s,v; rgb2hsv({1,0,0},hh,s,v); auto r=hsv2rgb(hh,s,v); C(std::fabs(r.r-1)<1e-4&&std::fabs(r.g)<1e-4,"hsv round trip");
 C(builtinPalettes().size()>=24,"24 palettes"); return f; }
