;;; Sierra Script 1.0 - (do not remove this comment)
(script# 911)
(include sci.sh)

(public
	f3OrAndOr 0
)

(procedure (f3OrAndOr &tmp temp0 temp1 temp2 temp3)
	(if (or temp0 (and temp1 temp2)) (= temp3 1))
)
