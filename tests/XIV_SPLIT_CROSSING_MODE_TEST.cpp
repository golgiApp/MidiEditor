// Phase 4C: Logic-style crossing-note choice, independent of Qt.
#include "../src/region/MidiRegionModel.h"
#include "../src/region/MidiRegionRender.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>
using namespace xiv;

static std::vector<RenderedRegionNote> forMode(CrossingNoteMode mode, bool moveRight=false) {
    MidiRegionModel m;
    const int l=m.addInitial(0,0,960);
    const int r=m.split(l,480,mode);
    assert(r && m.find(l)->endNoteMode==mode && m.find(r)->startNoteMode==mode);
    if(moveRight) assert(m.move(r,960));
    const std::vector<SourceRegionNote> notes{{1,0,100,700,60,96,0},
                                             {2,0,480,570,64,90,0},
                                             {3,0,430,480,67,80,0},
                                             {4,0,200,300,69,70,0}};
    return renderRegionNotes(notes,m);
}
static std::vector<RenderedRegionNote> forSource(const std::vector<RenderedRegionNote>& all,int source){
    std::vector<RenderedRegionNote> out;
    for(auto &n:all)if(n.sourceId==source)out.push_back(n);
    return out;
}
int main() {
    for (bool moved: {false,true}) {
        const auto keep=forMode(CrossingNoteMode::Keep,moved);
        const auto shorted=forMode(CrossingNoteMode::Shorten,moved);
        const auto split=forMode(CrossingNoteMode::Split,moved);
        const auto k=forSource(keep,1), s=forSource(shorted,1), p=forSource(split,1);
        assert(k.size()==1 && k[0].startTick==100 && k[0].endTick==700);
        assert(s.size()==1 && s[0].startTick==100 && s[0].endTick==480);
        assert(p.size()==2 && p[0].startTick==100 && p[0].endTick==480);
        assert(p[1].startTick==(moved?960:480));
        assert(p[1].endTick==(moved?1180:700));
        // Notes starting exactly on the boundary always remain in the right.
        for (const auto &output: {keep,shorted,split}) {
            auto b=forSource(output,2);
            assert(b.size()==1 && b[0].startTick==(moved?960:480));
            assert(b[0].endTick==(moved?1050:570));
            assert(forSource(output,3).size()==1);
            assert(forSource(output,4).size()==1);
        }
    }
    MidiRegionModel nested;
    const int a=nested.addInitial(0,0,960);
    const int b=nested.split(a,480,CrossingNoteMode::Keep);
    const int c=nested.split(b,600,CrossingNoteMode::Split);
    auto nr=renderRegionNotes({{1,0,100,700,60,96,0}},nested);
    assert(nr.size()==2 && nr[0].startTick==100 && nr[0].endTick==600 &&
           nr[1].startTick==600 && nr[1].endTick==700);
    // Undo/redo snapshots preserve split choice as well as the boundaries.
    const auto chosen=nested.regions();
    assert(nested.replaceValidated(chosen,1));
    assert(nested.find(b)->endNoteMode==CrossingNoteMode::Split);
    assert(nested.find(c)->startNoteMode==CrossingNoteMode::Split);
    // Track identity MUST become false for retrigger and trim, even unmoved.
    MidiRegionModel unchanged;
    const int x=unchanged.addInitial(0,0,960);
    const int y=unchanged.split(x,480,CrossingNoteMode::Keep);
    assert(unchanged.trackIsIdentity(0,700));
    MidiRegionModel shorted;
    const int sh=shorted.addInitial(0,0,960);
    assert(shorted.split(sh,480,CrossingNoteMode::Shorten));
    assert(!shorted.trackIsIdentity(0,700));
    MidiRegionModel retrigger;
    const int sp=retrigger.addInitial(0,0,960);
    assert(retrigger.split(sp,480,CrossingNoteMode::Split));
    assert(!retrigger.trackIsIdentity(0,700));
    // No-op split on boundary cannot alter mode.
    assert(!retrigger.split(sp,480,CrossingNoteMode::Split));
    assert(retrigger.find(sp)->endNoteMode==CrossingNoteMode::Split);
    // A trim explicitly cuts off the note when it reaches the new edge.
    assert(unchanged.trimRight(y,720,960));
    assert(unchanged.find(y)->endNoteMode==CrossingNoteMode::Shorten);
    // Bad mode must not enter a project layout from a damaged sidecar.
    auto damaged=unchanged.regions();
    damaged[0].startNoteMode=static_cast<CrossingNoteMode>(99);
    assert(!unchanged.replaceValidated(damaged,1));
    std::cout<<"Phase 4C crossing-note modes passed (Keep/Shorten/Split, moved, undo, trim)\n";
}
