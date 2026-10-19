#include "AudioDrive.h"
#include <cstdio>
using namespace monie;
int main(){ int f=0; auto C=[&](bool c,const char*m){ if(!c){++f;printf("FAIL: ");} else printf("ok:   "); printf("%s\n",m);};
 AudioDrive a; RawAudio r; DriveParams p; r.bass=1; for(int i=0;i<60;i++) a.update(1/60.f,r,p); C(a.update(1/60.f,r,p).bass>.8f,"bass rises to input");
 r.bass=0; float v=0; for(int i=0;i<120;i++) v=a.update(1/60.f,r,p).bass; C(v<.05f,"bass decays when silent");
 r.beatCount=1; auto d=a.update(1/60.f,r,p); C(d.beatNow&&d.beat>0.5f&&d.beatAge==0.f&&d.beatId==1,"beat triggers envelope and id");
 d=a.update(1/60.f,r,p); C(!d.beatNow&&d.beat<d.bySource(9)+1e-6f&&d.beatAge>0,"beat edge only once");
 AudioDrive b; DriveParams hi; hi.sensBass=2; RawAudio r2; r2.bass=.3f; for(int i=0;i<60;i++){a.update(1/60.f,r2,p); b.update(1/60.f,r2,hi);} C(b.update(1/60.f,r2,hi).bass>a.update(1/60.f,r2,p).bass*1.5f,"sensitivity scales");
 AudioDrive c1,c2; RawAudio r3; r3.rms=.5f; r3.bass=.4f; for(int i=0;i<30;i++){c1.update(1/60.f,r3,p);c2.update(1/60.f,r3,p);} C(c1.update(.016f,r3,p).bass==c2.update(.016f,r3,p).bass,"deterministic (offline == live)");
 return f; }
