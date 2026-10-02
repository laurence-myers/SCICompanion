;;; Sierra Script 1.0 - (do not remove this comment)
(script# 945)
(include sci.sh)

(public
	s1SwitchValue 0
)

(procedure (s1SwitchValue param1 &tmp temp0)
	(= temp0
		(switch param1
			(1 10)
			(2 20)
			(else  30)
		))
)
