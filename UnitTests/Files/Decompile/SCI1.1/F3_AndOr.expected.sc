;;; Sierra Script 1.0 - (do not remove this comment)
(script# 912)
(include sci.sh)

(public
	f3AndOr 0
)

(procedure (f3AndOr &tmp temp0 temp1 temp2 temp3)
	(if (and temp0 (or temp1 temp2)) (= temp3 1))
)
