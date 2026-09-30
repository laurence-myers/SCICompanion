;;; Sierra Script 1.0 - (do not remove this comment)
(script# 944)
(include sci.sh)

(public
	f20OrAndInLoop 0
	f20OrTwoAnds 1
)

(procedure (f20OrAndInLoop param1 param2 &tmp temp0)
	(while (> (-- temp0) 0)
		(if
		(or (== param1 0) (and (== param1 1) (> temp0 2)))
			(= param2 1)
		)
	)
)

(procedure (f20OrTwoAnds param1 param2 &tmp temp0 temp1)
	(while param1
		(if
			(or
				(and (not param2) (>= temp0 temp1))
				(and param2 (<= temp0 temp1))
			)
			(++ temp0)
		)
	)
	(return temp0)
)
