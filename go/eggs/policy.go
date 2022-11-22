package eggs

import (
	"time"
	"xtx/eggsfs/msgs"
)

// If multiple policies are present, the file will be deleted if
// any of the policies are not respected. If neither policy is present,
// snapshots will never be deleted.
type SnapshotPolicy struct {
	// Keep all files/directories versions with a certain name within this time window.
	// If zero, this kind of policy is inactive.
	DeleteAfterTime time.Duration
	// Keep last N file/directory versions with a certain name. If 0, this
	// kind of policy is inactive.
	DeleteAfterVersions int
}

// The edges are the entirety of the edges for a certain file name in a certain dir.
// Oldest edge first.
//
// It is assumed that every delete in the input will be be preceeded by a non-delete.
//
// If it returns N, edges[N:] will be well formed too.
func (policy *SnapshotPolicy) edgesToRemove(now msgs.EggsTime, edges []msgs.Edge) int {
	if len(edges) == 0 {
		return 0
	}
	// Index dividing edges, so that all all edges[i] i < firstGoodEdgeVersions should be
	// removed, while all edges[i] i >= firstGoodEdgeVersions should be kept.
	firstGoodEdgeVersions := 0
	// Note that DeleteAfterVersions is a bit tricky: we don't know here if there even
	// is a current edge. So depending on whether there is or not, results might differ.
	//
	// We delete conservative (assuming that there _is_ a current edge). This prevents us from
	// using policy.DeleteAfterVersions=1 to immediately purge deleted versions, we might
	// want to revise this.
	if policy.DeleteAfterVersions > 0 {
		versionNumber := 0
		for firstGoodEdgeVersions = len(edges) - 1; firstGoodEdgeVersions >= 0; firstGoodEdgeVersions-- {
			// ignore deletes, we just want to keep the last N versions.
			if edges[firstGoodEdgeVersions].TargetId.Id() == msgs.NULL_INODE_ID {
				continue
			}
			versionNumber++
			// the latest version number is the latest to keep
			if versionNumber >= policy.DeleteAfterVersions {
				break
			}
		}
	}
	firstGoodEdgeTime := 0
	if policy.DeleteAfterTime > time.Duration(0) {
		for firstGoodEdgeTime = len(edges) - 1; firstGoodEdgeTime >= 0; firstGoodEdgeTime-- {
			// if this file was created before the cutoff, then it is the last one to
			// matter.
			creationTime := edges[firstGoodEdgeTime].CreationTime.Time()
			if now.Time().Sub(creationTime) > policy.DeleteAfterTime {
				break
			}
		}
	}
	firstGoodEdge := Max(0, Max(firstGoodEdgeVersions, firstGoodEdgeTime))
	// if the last edge is a delete, remove that too (we can't keep a delete hanging)
	if edges[firstGoodEdge].TargetId.Id() == msgs.NULL_INODE_ID {
		firstGoodEdge++
	}
	return firstGoodEdge
}
