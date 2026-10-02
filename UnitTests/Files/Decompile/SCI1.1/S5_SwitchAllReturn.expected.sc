;;; Sierra Script 1.0 - (do not remove this comment)
(script# 949)
(include sci.sh)

(public
	s5SwitchAllReturn 0
)

(procedure (s5SwitchAllReturn param1)
	(switch param1
		(1 (return 5))
		(2 (return 7))
		(else  (return 6))
	)
)
