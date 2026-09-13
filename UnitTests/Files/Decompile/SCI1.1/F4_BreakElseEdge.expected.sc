;;; Sierra Script 1.0 - (do not remove this comment)
(script# 904)
(include sci.sh)

(public
	f4BreakElseEdge 0
)

(procedure (f4BreakElseEdge &tmp temp0 temp1 temp2)
	(repeat
		(if (or temp0 temp1)
			(if (and temp0 temp1) (= temp2 1))
			(break)
		)
		(= temp2 0)
	)
)
