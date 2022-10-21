package main

import (
	"fmt"
	"time"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/utils"
)

// If multiple items are specified, the file will be kept if any policy matches.
type policy struct {
	// Keep all files/directories versions with a certain name within this time window.
	// If zero, this kind of policy is inactive.
	keepWithin time.Duration
	// Keep last N file/directory versions with a certain name. If 0, this
	// kind of policy is inactive.
	keepLast int
}

// The edges are the entirety of the edges for a certain file name in a certain dir.
// Oldest edge first.
//
// The input is assumed to be well formed:
//
// * At least of length 1
// * Every delete edge is preceded by a non-delete
//
// If it returns N, edges[N:] will be well formed too.
func (policy *policy) edgesToRemove(now msgs.EggsTime, edges []msgs.EdgeWithOwnership) int {
	if len(edges) == 0 {
		panic("cannot remove edges if there are no edges!")
	}
	hasPolicy := false
	// Index dividing edges, so that all all edges[i] i < lastEdgeToKeep should be
	// removed, while all edges[i] i >= lastEdgeToKeep should be kept.
	lastEdgeToKeep := len(edges) - 1
	// First rewind enough to keep N last versions.
	if policy.keepLast > 0 {
		hasPolicy = true
		versionNumber := 0
		for ; lastEdgeToKeep >= 0; lastEdgeToKeep-- {
			// ignore deletes, we just want to keep the last N versions.
			if edges[lastEdgeToKeep].TargetId.Id() == msgs.NULL_INODE_ID {
				continue
			}
			versionNumber++
			// the latest version number is the latest to keep
			if versionNumber >= policy.keepLast {
				break
			}
		}
	}
	// Then rewind enough to go beyond the time window.
	if policy.keepWithin > time.Duration(0) {
		hasPolicy = true
		for ; lastEdgeToKeep >= 0; lastEdgeToKeep-- {
			// if this file was created before the cutoff, then it is the last one to
			// matter.
			creationTime := edges[lastEdgeToKeep].CreationTime.Time()
			if now.Time().Sub(creationTime) > policy.keepWithin {
				break
			}
		}
	}
	lastEdgeToKeep = utils.Max(0, lastEdgeToKeep)
	// if the last edge is a delete, remove that too (we can't keep a delete hanging)
	if edges[lastEdgeToKeep].TargetId.Id() == msgs.NULL_INODE_ID {
		lastEdgeToKeep++
	} else {
		// If the latest edge was _not_ a delete, we must not delete it, since it's
		// the current edge
		if lastEdgeToKeep == len(edges) {
			panic(fmt.Errorf("policy is unexpectedly removing the current edge!"))
		}
	}
	if !hasPolicy {
		panic(fmt.Errorf("no policy in %+v", policy))
	}
	return lastEdgeToKeep
}
