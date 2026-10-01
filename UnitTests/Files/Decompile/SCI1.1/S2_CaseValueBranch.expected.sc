;;; Sierra Script 1.0 - (do not remove this comment)
(script# 946)
(include sci.sh)

(public
	s2CaseValueBranch 0
)

(procedure (s2CaseValueBranch param1 param2 &tmp temp0)
	(switch param1
		((if param2 1 else 2)
			(= temp0 5)
		)
		(3 (= temp0 6))
	)
)
