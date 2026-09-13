;;; Sierra Script 1.0 - (do not remove this comment)
(script# 923)
(include sci.sh)

(public
	n1ChainedCompare 0
)

(procedure (n1ChainedCompare param1 &tmp temp0)
	(= temp0 0)
	(if (< 0 param1 19) (= temp0 1))
	(if (<= 1 param1 15) (++ temp0))
	(if (and (< param1 5) (> temp0 10)) (= temp0 2))
	(return (< 5 param1 10))
)
