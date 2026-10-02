;;; Sierra Script 1.0 - (do not remove this comment)
(script# 967)
(include sci.sh)

(public
	v4LoopValue 0
	v4LoopValueBreak 1
)

(procedure (v4LoopValue param1 &tmp temp0 temp1)
	(= temp0 (Random 1 15))
	(repeat
		(= temp1 0)
		(if
			(while (< temp1 12)
				(if (== temp0 param1) (break) else (++ temp1))
			)
			(= temp0 (Random 1 15))
		else
			(break)
		)
	)
	(return temp0)
)

(procedure (v4LoopValueBreak param1 &tmp temp0)
	(if (repeat
		(++ temp0)
		(if param1 else (break))
	)
		(return 1)
	)
	(return 2)
)
