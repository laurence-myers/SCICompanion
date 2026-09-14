;;; Sierra Script 1.0 - (do not remove this comment)
(script# 929)
(include sci.sh)
(use Main)
(use Cycle)
(use System)


(class Sync of Obj
	(properties
		syncTime -1
		syncCue -1
		prevCue -1
		syncNum -1
	)
	
	(method (syncStart param1 param2 param3 param4 param5)
		(DoSync 0 self param1 param2 param3 param4 param5)
		(if (!= syncCue -1) (= prevCue syncCue) (= syncTime 0))
	)
	
	(method (syncCheck)
		(if
			(and
				(!= syncCue -1)
				(or (u<= syncTime gSyncBias) (<= syncTime (DoAudio 6)))
			)
			(if (== (& $fff0 syncCue) 0)
				(= prevCue (| (& prevCue $fff0) syncCue))
			else
				(= prevCue syncCue)
			)
			(DoSync 1 self)
		)
	)
	
	(method (syncStop)
		(= prevCue -1)
		(DoSync 2)
	)
)

(class MouthSync of Cycle
	(properties
		client 0
		caller 0
		cycleDir 1
		cycleCnt 0
		completed 0
	)
	
	(method (init param1 param2 param3 param4 param5 param6)
		(super init: param1)
		(if (IsObject gTheSync) (gTheSync syncStop: dispose:))
		(= gTheSync (Sync new:))
		(gTheSync syncStart: param2 param3 param4 param5 param6)
	)
	
	(method (doit &tmp temp0 gTheSyncSyncTime_2 gTheSyncSyncTime temp3)
		(super doit:)
		(if (!= (gTheSync prevCue?) -1)
			(= gTheSyncSyncTime (gTheSync syncTime?))
			(= temp3 0)
			(repeat
				(= gTheSyncSyncTime_2 (gTheSync syncTime?))
				(gTheSync syncCheck:)
				(if (== gTheSyncSyncTime_2 (gTheSync syncTime?))
					(break)
				)
			)
			(if
				(and
					(!= gTheSyncSyncTime (gTheSync syncTime?))
					(!=
						(client cel?)
						(= temp0 (& $000f (gTheSync prevCue?)))
					)
				)
				(client cel: temp0)
			)
		else
			(= completed 1)
			(self cycleDone:)
		)
	)
	
	(method (dispose)
		(super dispose:)
		(if gTheSync (gTheSync dispose:) (= gTheSync 0))
	)
	
	(method (cue)
		(if gTheSync
			(gTheSync syncStop: dispose:)
			(= gTheSync 0)
			(if caller (caller cue:) (= caller 0))
		)
	)
)
