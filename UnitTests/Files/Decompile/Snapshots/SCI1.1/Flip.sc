;;; Sierra Script 1.0 - (do not remove this comment)
(script# 926)
(include sci.sh)
(use Main)
(use System)

(public
	FlipPolygons 0
	FlipFeature 1
)

(procedure (FlipPolygons theGRoomObstacles &tmp gRoomObstacles)
	(cond 
		((not argc) (= gRoomObstacles (gRoom obstacles?)))
		((theGRoomObstacles isKindOf: Collect) (= gRoomObstacles theGRoomObstacles))
		(else (theGRoomObstacles perform: flipPoly) (return))
	)
	(gRoomObstacles eachElementDo: 96 flipPoly)
	(DisposeScript 926)
)

(procedure (FlipFeature param1 &tmp temp0)
	(if (not argc)
		(gFeatures eachElementDo: 96 flipFeature)
	else
		(= temp0 0)
		(while (< temp0 argc)
			(if ([param1 temp0] isKindOf: Collect)
				([param1 temp0] eachElementDo: 96 flipFeature)
			else
				([param1 temp0] perform: flipFeature)
			)
			(++ temp0)
		)
	)
	(DisposeScript 926)
)

(instance flipPoly of Code
	(properties)
	
	(method (doit param1 &tmp temp0 temp1 temp2)
		(= temp2 (param1 size?))
		(= temp1 (Memory 1 (* 4 temp2)))
		(= temp0 0)
		(while (< temp0 temp2)
			(Memory
				6
				(+ temp1 (* 4 temp0))
				(-
					320
					(Memory
						5
						(-
							(+ (param1 points?) (* 4 temp2))
							(+ 4 (* 4 temp0))
						)
					)
				)
			)
			(Memory
				6
				(+ temp1 (* 4 temp0) 2)
				(Memory
					5
					(-
						(+ (param1 points?) (* 4 temp2))
						(+ 2 (* 4 temp0))
					)
				)
			)
			(++ temp0)
		)
		(if (param1 dynamic?) (Memory 3 (param1 points?)))
		(param1 points: temp1 dynamic: 1)
	)
)

(instance flipFeature of Code
	(properties)
	
	(method (doit param1 &tmp temp0)
		(if (IsObject (param1 onMeCheck?))
			(FlipPolygons (param1 onMeCheck?))
		else
			(= temp0 (param1 nsLeft?))
			(param1
				nsLeft: (- 320 (param1 nsRight?))
				nsRight: (- 320 temp0)
			)
		)
	)
)
