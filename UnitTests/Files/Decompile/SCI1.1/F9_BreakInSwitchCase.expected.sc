;;; Sierra Script 1.0 - (do not remove this comment)
(script# 921)
(include sci.sh)

(public
	f9BreakInSwitchCase 0
)

(procedure (f9BreakInSwitchCase param1 &tmp temp0 temp1)
	(= temp0 0)
	(= temp1 0)
	(while (< temp0 param1)
		(switch (& temp0 $0003)
			(0 (= temp1 10))
			(1 (= temp1 20))
			(2 (break))
		)
		(++ temp0)
	)
	(return temp1)
)
