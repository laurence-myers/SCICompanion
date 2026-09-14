;;; Sierra Script 1.0 - (do not remove this comment)
(script# 946)
(include sci.sh)
(use System)


(class Polygon of Obj
	(properties
		size 0
		points 0
		type 1
		dynamic 0
	)
	
	(method (init param1 &tmp [temp0 2])
		(= size (/ argc 2))
		(= points (Memory 1 (* 2 argc)))
		(StrCpy points @param1 (- (* argc 2)))
		(= dynamic 1)
	)
	
	(method (dispose)
		(if (and dynamic points) (Memory 3 points))
		(super dispose:)
	)
)
