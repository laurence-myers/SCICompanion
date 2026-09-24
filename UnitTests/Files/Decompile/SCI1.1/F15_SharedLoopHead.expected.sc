;;; Sierra Script 1.0 - (do not remove this comment)
(script# 937)
(include sci.sh)

(public
	f15SharedLoopHead 0
)

(procedure (f15SharedLoopHead param1 &tmp temp0 temp1)
	(repeat
		(while (< temp0 10)
			(if param1
				(break)
			else
				(++ temp0)
				(if temp1 (++ temp1))
			)
		)
		(if (> temp1 5) (break) else (++ temp1))
	)
)
