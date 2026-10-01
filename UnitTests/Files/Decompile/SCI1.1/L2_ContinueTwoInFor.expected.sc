;;; Sierra Script 1.0 - (do not remove this comment)
(script# 962)
(include sci.sh)

(public
	l2ContinueTwoInFor 0
)

(procedure (l2ContinueTwoInFor param1 &tmp temp0 temp1)
	(= temp0 0)
	(for () (< temp0 param1) ((++ temp0))
		(= temp1 0)
		(while (< temp1 5)
			(if (== temp1 temp0) (continue 2))
			(++ temp1)
		)
		(= temp1 9)
	)
)
