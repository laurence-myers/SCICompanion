;;; Sierra Script 1.0 - (do not remove this comment)
(script# 948)
(include sci.sh)

(public
	s4EmptyLastCaseInLoop 0
)

(procedure (s4EmptyLastCaseInLoop param1 &tmp temp0)
	(while (< temp0 param1)
		(switch param1
			(1 (= temp0 5))
			(2)
		)
		(++ temp0)
	)
)
