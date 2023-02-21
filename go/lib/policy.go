package lib

import (
	"xtx/eggsfs/msgs"
)

// Returns how many edges to remove according to the policy (as a prefix of the input).
//
// `edges` should be all the snapshot edges for a certain directory, oldest edge first.
//
// It is assumed that every delete in the input will be be preceeded by a non-delete.
//
// If it returns N, edges[N:] will be well formed too in the sense above.
func edgesToRemove(policy *msgs.SnapshotPolicy, now msgs.EggsTime, edges []msgs.Edge) int {
	if len(edges) == 0 {
		return 0
	}
	// Index dividing edges, so that all all edges[i] i < firstGoodEdgeVersions should be
	// removed, while all edges[i] i >= firstGoodEdgeVersions should be kept.
	firstGoodEdgeVersions := 0
	// Note that DeleteAfterVersions only affects the snapshot edges: we don't look at the
	// current edge at all.
	//
	// This means, for example, that if we have DeleteAfterVersions=5, after applying the policy
	// there might be 5 or 6 edges with a certain name, depending on whether the a current
	// edge for it exists.
	if policy.DeleteAfterVersions.Active() {
		versionNumber := 0
		for firstGoodEdgeVersions = len(edges) - 1; firstGoodEdgeVersions >= 0; firstGoodEdgeVersions-- {
			// ignore deletes, we just want to keep the last N versions.
			if edges[firstGoodEdgeVersions].TargetId.Id() == msgs.NULL_INODE_ID {
				continue
			}
			if versionNumber >= int(policy.DeleteAfterVersions.Versions()) {
				firstGoodEdgeVersions++
				break
			}
			versionNumber++
		}
	}
	firstGoodEdgeTime := 0
	if policy.DeleteAfterTime.Active() {
		for firstGoodEdgeTime = len(edges) - 1; firstGoodEdgeTime >= 0; firstGoodEdgeTime-- {
			creationTime := edges[firstGoodEdgeTime].CreationTime.Time()
			if now.Time().Sub(creationTime) > policy.DeleteAfterTime.Time() {
				firstGoodEdgeVersions++
				break
			}
		}
	}
	// max? what's that?
	firstGoodEdge := 0
	if firstGoodEdgeVersions > firstGoodEdge {
		firstGoodEdge = firstGoodEdgeVersions
	}
	if firstGoodEdgeTime > firstGoodEdge {
		firstGoodEdge = firstGoodEdgeTime
	}
	// if the last edge is a delete, remove that too (we can't keep a delete hanging)
	if firstGoodEdge < len(edges) && edges[firstGoodEdge].TargetId.Id() == msgs.NULL_INODE_ID {
		firstGoodEdge++
	}
	return firstGoodEdge
}
