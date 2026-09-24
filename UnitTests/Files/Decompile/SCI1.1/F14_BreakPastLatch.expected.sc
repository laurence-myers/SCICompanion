;;; Sierra Script 1.0 - (do not remove this comment)
(script# 936)
(include sci.sh)

(public
	f14BreakPastLatch 0
)

(procedure (f14BreakPastLatch param1 &tmp temp0 temp1 temp2)
	(repeat
		(if param1
			(if temp0 (break))
		else
			(= temp1 0)
			(repeat
				(= temp2 0)
				(while (< temp2 4)
					(++ temp2)
				)
				(if temp0 (++ temp1))
				(if (>= temp1 16) (break) else (= temp0 temp1))
			)
			(break)
		)
		(= temp0 1)
	)
)
