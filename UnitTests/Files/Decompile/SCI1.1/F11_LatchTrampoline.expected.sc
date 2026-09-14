;;; Sierra Script 1.0 - (do not remove this comment)
(script# 924)
(include sci.sh)

(public
	f11LatchTrampoline 0
)

(procedure (f11LatchTrampoline param1 &tmp temp0 temp1)
	(= temp0 0)
	(= temp1 0)
	(while (< temp0 param1)
		(++ temp0)
		(cond 
			((== temp0 1)
				(if (& temp0 $0002) (= temp1 1) (continue))
				(if (& temp0 $0004) (= temp1 2))
			)
			((== temp0 3) (if (== temp1 0) (= temp1 3) (break)))
			(else (= temp1 4))
		)
	)
	(return temp1)
)
