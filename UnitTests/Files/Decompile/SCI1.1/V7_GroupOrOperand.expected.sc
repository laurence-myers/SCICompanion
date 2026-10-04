;;; Sierra Script 1.0 - (do not remove this comment)
(script# 957)
(include sci.sh)

(public
	v7GroupOrOperand 0
	v7GroupOrStatement 1
)

(procedure (v7GroupOrOperand param1 &tmp temp0)
	(if
		(or
			(not param1)
			(
				(= temp0 17)
				(not
					(while (<= temp0 24)
						(if (== temp0 param1) (return 1) (Random))
						(++ temp0)
					)
				)
			)
		)
		(= temp0 2)
	)
	(return 0)
)

(procedure (v7GroupOrStatement param1 &tmp temp0)
	(if (not param1) (= temp0 17) (Random))
	(= temp0 2)
)
