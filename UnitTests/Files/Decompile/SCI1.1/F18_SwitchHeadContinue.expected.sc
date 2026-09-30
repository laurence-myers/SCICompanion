;;; Sierra Script 1.0 - (do not remove this comment)
(script# 942)
(include sci.sh)

(public
	f18SwitchHeadContinue 0
)

(procedure (f18SwitchHeadContinue param1 param2 param3 &tmp temp0 temp1)
	(repeat
		(= temp0
			(switch param1
				(0 5)
				(else  6)
			))
		(cond 
			((== temp0 5) (if param2 (= temp1 1)))
			((and (== temp0 6) (== param1 2)) (if param3 (= temp1 2) (break)))
			(param3 (= temp1 3) (break))
		)
	)
	(return temp1)
)
