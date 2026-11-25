#pragma once
// Transfer-safety semantics ported from BEI (Barely Enough Items).
//
//   Source : https://github.com/MiracleForest/BarelyEnoughItems
//   Commit : 410ecfdeb794dd1b7d7844c0e3ea256091b069c8 (release v0.1.2)
//   Licence: LGPL-3.0-or-later (c) MiracleForest; used here under GPL-3.0
//            (LGPLv3 section 2 permits relicensing into GPLv3).
//   Original: src/actions/StationTransfer.cpp
//             transfer()            :351-411 (refuse with a cursor item; :355)
//             moveToInventory()     :167-194 (same-item stacks first, then empty slots)
//             hasCursorItem()       :161-165
//             write-back loop       :396-401 ("Don't leave half-filled slots behind")
//
// Changes from the original (all of them structural, none behavioural):
//   * keyed on Praxis's StackValue/MoveStep instead of BEI's ItemKey/SlotData;
//   * split from the game layer so that it is a pure, host-testable core policy;
//   * the rollback write-back is a *step emitter* like planTransfer(), so the existing
//     InventoryActions job loop applies it through the same handlePlaceAmount path and
//     collects its own rollback evidence, rather than BEI's immediate native loop;
//   * writes are bounded by InventoryTransferOptions::maxSteps (BEI is unbounded).
//
// This header adds no native dependency and does not alter planTransfer().
#include "core/InventoryTransferPolicy.h"
namespace praxis::companion {
namespace inventory {
// BEI's reject-when-the-cursor-holds-an-item rule, as a value predicate.
// `cursorCount` is the live count of the native cursor stack, or -1 when the cursor
// could not be observed. -1 fails closed: BEI reads the stack unconditionally and
// refuses only when it is non-empty; treating "unobservable" as "occupied" is the
// stricter reading of the same rule (see InventoryActions context()).
inline bool transferAdmittedByCursor(int cursorCount) noexcept {return cursorCount==0;}
// Which side of a transfer owns the "half-filled" slots that must be evacuated on
// failure. BEI moves station inputs back into the player inventory; the Praxis
// equivalent is the destination region of the transfer (storage for an inventory
// source, the player inventory for a storage source).
inline bool rollbackDestinationIsStorage(int hovered) noexcept {return hovered>=0&&hovered<36;}
// Steps that move every non-empty slot of `from` back into `to`, reusing BEI's
// "same item with room first, then empty slots" ordering. Pure: reads both regions as
// they are now, does not mutate them. Returns an empty vector when there is nothing to
// return, and nullopt when the write-back cannot be completed -- either because it
// would exceed `maxSteps`, or because the destination has no room left. A partial
// restore is never returned: the caller must treat nullopt as "still stranded".
inline std::optional<std::vector<MoveStep>> planTransferRollback(
    std::vector<StackValue> const& from,std::vector<StackValue> const& to,int maxSteps) {
    if(maxSteps<1||!validStacks(from)||!validStacks(to))return std::nullopt;
    // One flat region, exactly like planTransfer(): 0..from.size()-1 is the source
    // region, from.size().. is the destination region. applyMove() therefore keeps all
    // of its own invariants (locked, stackable, maximum) on the ported steps.
    auto values=from;values.insert(values.end(),to.begin(),to.end());
    int const sourceCount=static_cast<int>(from.size());
    int const total=static_cast<int>(values.size());
    std::vector<MoveStep> steps;steps.reserve(static_cast<std::size_t>(maxSteps));
    // Pass 0: top up stacks of the same item that still have room (BEI pass == 0).
    // Pass 1: then use empty destination slots.
    for(int pass=0;pass<2;++pass)
        for(int sourceIndex=0;sourceIndex<sourceCount;++sourceIndex) {
            // A locked slot is pinned by the user's sort exclusion: applyMove() refuses to
            // move out of it, so it must never appear in a write-back step either.
            if(values[sourceIndex].locked)continue;
            while(values[sourceIndex].count>0) {
                auto const stack=values[sourceIndex];
                int target=-1,amount=0;
                for(int i=sourceCount;i<total;++i) {
                    auto const& slot=values[i];
                    if(slot.locked)continue;
                    if(pass==0) {
                        if(slot.count<=0||slot.key!=stack.key||slot.maximum!=stack.maximum)continue;
                        int const free=slot.maximum-slot.count;
                        if(free<=0)continue;
                        if(free>amount){target=i;amount=free;}
                    }else {
                        if(slot.count!=0)continue;
                        target=i;amount=stack.count;break;
                    }
                }
                if(target<0||amount<=0)break;
                if(static_cast<int>(steps.size())>=maxSteps)return std::nullopt;
                MoveStep step{sourceIndex,target,std::min(amount,values[sourceIndex].count),false};
                if(!applyMove(values,step))return std::nullopt;
                steps.push_back(step);
            }
        }
    if(steps.empty()){
        // Nothing was movable: a clean no-op only when there is nothing to return.
        for(int i=0;i<sourceCount;++i)if(!values[i].locked&&values[i].count>0)return std::nullopt;
        return std::vector<MoveStep>{};
    }
    // BEI returns true only when the slot really ends up empty; mirror that: a
    // write-back that left anything behind is not a rollback. Locked slots are exempt
    // because applyMove() is contractually forbidden from touching them.
    for(int i=0;i<sourceCount;++i)if(!values[i].locked&&values[i].count>0)return std::nullopt;
    return steps;
}
}
}
