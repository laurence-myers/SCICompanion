;;; Sierra Script 1.0 - (do not remove this comment)
(script# 933)
(include sci.sh)

(public
	n2SierraChainedCompare 0
)

(procedure (n2SierraChainedCompare param1 param2 param3 &tmp temp0)
	(if (<= param2 (param1 x?) param3) (= temp0 1))
	(return temp0)
)
