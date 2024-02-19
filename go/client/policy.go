package client

import (
	"fmt"
	"xtx/eggsfs/msgs"
)

// Returns how many edges to remove according to the policy (as a prefix of the input).
//
// `edges` should be all the snapshot edges for a certain directory, oldest edge first,
// possibly trailed by the current edge. No other current edges can be present.
//
// It is assumed that every delete in the input will be be preceeded by a non-delete.
//
// If it returns N, edges[N:] will be well formed too in the sense above.
func edgesToRemove(policy *msgs.SnapshotPolicy, now msgs.EggsTime, edges []msgs.Edge) int {
	if len(edges) == 0 {
		return 0
	}
	// Do not consider the trailing current edge, if present.
	if edges[len(edges)-1].Current {
		edges = edges[:len(edges)-1]
	}
	// Index dividing edges, so that all all edges[i] i < firstGoodEdgeVersions should be
	// removed, while all edges[i] i >= firstGoodEdgeVersions should be kept.
	var firstGoodEdgeVersions int
	// Note that DeleteAfterVersions only affects the snapshot edges: we don't look at the
	// current edge at all.
	//
	// This means, for example, that if we have DeleteAfterVersions=5, after applying the policy
	// there might be 5 or 6 edges with a certain name, depending on whether the a current
	// edge for it exists.
	if policy.DeleteAfterVersions.Active() {
		versionNumber := 0
		for firstGoodEdgeVersions = len(edges) - 1; firstGoodEdgeVersions >= 0; firstGoodEdgeVersions-- {
			edge := edges[firstGoodEdgeVersions]
			if edge.Current {
				panic(fmt.Errorf("unexpected current edge: %v", edge))
			}
			// ignore deletes, we just want to keep the last N versions.
			if edge.TargetId.Id() == msgs.NULL_INODE_ID {
				continue
			}
			if versionNumber >= int(policy.DeleteAfterVersions.Versions()) {
				firstGoodEdgeVersions++
				break
			}
			versionNumber++
		}
	}
	var firstGoodEdgeTime int
	if policy.DeleteAfterTime.Active() {
		for firstGoodEdgeTime = len(edges) - 1; firstGoodEdgeTime >= 0; firstGoodEdgeTime-- {
			edge := edges[firstGoodEdgeTime]
			if edge.Current {
				panic(fmt.Errorf("unexpected current edge: %v", edge))
			}
			creationTime := edge.CreationTime.Time()
			if now.Time().Sub(creationTime) > policy.DeleteAfterTime.Time() {
				firstGoodEdgeTime++
				break
			}
		}
	}
	// Remove as much as possible (i.e. the rules are a disjuntion)
	firstGoodEdge := firstGoodEdgeVersions
	if firstGoodEdgeTime > firstGoodEdge {
		firstGoodEdge = firstGoodEdgeTime
	}
	if firstGoodEdge < 0 {
		firstGoodEdge = 0
	}
	// if the last edge is a delete, remove that too (we can't keep a delete hanging)
	if firstGoodEdge < len(edges) && edges[firstGoodEdge].TargetId.Id() == msgs.NULL_INODE_ID {
		firstGoodEdge++
	}
	return firstGoodEdge
}
