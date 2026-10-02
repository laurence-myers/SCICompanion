;;; Sierra Script 1.0 - (do not remove this comment)
(script# 963)
(include sci.sh)

(public
	s6NoOpCaseTest 0
)

(procedure (s6NoOpCaseTest param1 &tmp temp0)
	(switch param1
		(4 (= temp0 5))
		(else 
			(== param1 1)
			(= temp0 7)
		)
	)
)
