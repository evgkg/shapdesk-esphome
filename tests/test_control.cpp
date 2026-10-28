#include "control.h"
#include <cassert>
#include <cstdio>
using namespace shapdesk_core;
struct Fake : Output {
  int key{0}; int64_t now{0}, deadline{0};
  bool press(int k,uint32_t ms) override { if(key) return false; key=k; deadline=now+ms*1000LL; return true; }
  void release() override {key=0;}
  bool active() const override {return key!=0;}
  void timer(int64_t t) { now=t; if(now>=deadline) release(); }
};
struct Rig {
  Fake out; Controller c{out};
  void sample(int height,int direction,int64_t t) {
    out.now=t; auto &s=c.tracking; s.known=s.current_known=true; s.current_held=false;
    s.current_height=height; s.last_rx_us=t; s.moving=direction!=0; s.motion_direction=direction;
    s.last_motion_us=direction?t:t-500000;
  }
  void tick(int64_t t,bool connected=true) {out.timer(t);c.tick(t,connected);}
};
int main() {
  for(int k=1;k<=7;++k) {
    assert(valid_press(k,300,20000));
    assert(valid_press(k,500,20000));
    assert(valid_press(k,501,20000)==!(k>=3&&k<=6));
  }
  assert(!valid_press(0,300,20000)&&!valid_press(8,300,20000));
  assert(!valid_press(1,-1,20000)&&!valid_press(1,20001,20000));
  {Rig r;assert(!r.c.move(100,0));r.sample(90,0,0);r.c.tracking.current_held=true;assert(!r.c.move(100,0));}
  {Rig r;r.sample(90,0,0);assert(r.c.move(100,0));assert(r.out.key==1);
    r.sample(95,1,100000);r.tick(100000);assert(r.out.key==1);
    r.sample(99,1,200000);r.tick(200000);assert(!r.out.key);
    r.sample(101,0,800000);r.tick(800000);assert(r.c.learned_offset[0]==1.25f);}
  {Rig r;r.sample(100,0,0);assert(r.c.move(90,0));assert(r.out.key==2);
    r.sample(95,-1,100000);r.tick(100000);r.sample(91,-1,200000);r.tick(200000);assert(!r.out.key);}
  {Rig r;r.sample(90,0,0);assert(r.c.move(100,0));
    for(int i=1;i<=29;i++){r.sample(90,0,i*100000);r.tick(i*100000);assert(r.out.key);}
    r.sample(91,1,2950000);r.tick(2950000);assert(r.out.key);}
  {Rig r;r.sample(90,0,0);r.c.move(100,0);
    for(int i=1;i<=30;i++){r.sample(90,0,i*100000);r.tick(i*100000);} assert(!r.out.key);}
  {Rig r;r.sample(90,0,0);r.c.move(100,0);r.sample(89,-1,100000);r.tick(100000);assert(!r.out.key);}
  {Rig r;r.sample(90,0,0);r.c.move(100,0);r.tick(500001);assert(!r.out.key);}
  {Rig r;r.sample(131,0,0);assert(!r.c.press_key(1,300,0));assert(r.c.press_key(2,300,0));r.c.stop();
    r.sample(66,0,0);assert(!r.c.press_key(2,300,0));assert(r.c.press_key(1,300,0));}
  {Rig r;assert(r.c.press_key(1,20000,0));auto deadline=r.out.deadline;
    assert(!r.c.press_key(1,20000,100));assert(r.out.deadline==deadline);
    assert(!r.c.press_key(2,300,100));assert(!r.out.key);}
  {Rig r;r.sample(90,0,0);r.c.move(100,0);assert(!r.c.move(110,100));assert(!r.out.key);}
  {Rig r;r.c.press_key(7,20000,0);r.out.timer(20000000);assert(!r.out.key);}
  {Rig r;r.c.press_key(3,500,0);assert(!r.c.press_key(3,501,1));r.out.timer(500000);assert(!r.out.key);}
  {Rig r;r.c.press_key(1,20000,0);r.tick(100,false);assert(!r.out.key);}
  {Rig r;r.sample(130,0,0);r.c.press_key(1,20000,0);r.sample(131,1,100000);r.tick(100000);assert(!r.out.key);}
  {Rig r;r.sample(90,0,0);assert(r.c.move(91,0));assert(!r.out.key);assert(!r.c.move(NAN,0));}
  puts("OK: limits, independent deadline model, conflicts, movement, coast, wake, faults");
}
