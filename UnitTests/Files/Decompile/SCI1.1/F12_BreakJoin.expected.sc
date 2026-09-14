;;; Sierra Script 1.0 - (do not remove this comment)
(script# 925)
(include sci.sh)

(public
	f12BreakJoin 0
)

(procedure (f12BreakJoin param1 &tmp temp0 temp1)
	(= temp0 0)
	(= temp1 0)
	(while (< temp0 param1)
		(++ temp0)
		(cond 
			((== temp0 1) (if (== temp0 2) (= temp1 1) else (= temp1 2) (break)))
			((== temp0 3) (= temp1 3))
		)
	)
	(return temp1)
)
