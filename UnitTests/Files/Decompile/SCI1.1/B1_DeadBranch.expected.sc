;;; Sierra Script 1.0 - (do not remove this comment)
(script# 929)
(include sci.sh)

(public
	b1DeadBranch 0
)

(procedure (b1DeadBranch param1 &tmp temp0)
	(if (and (> param1 17) (< param1 21))
		(= temp0 1)
	else
		(= temp0 2)
	)
)
