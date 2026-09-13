;;; Sierra Script 1.0 - (do not remove this comment)
(script# 922)
(include sci.sh)

(public
	f10MidBodyContinue 0
)

(procedure (f10MidBodyContinue param1 &tmp temp0 temp1)
	(= temp0 0)
	(= temp1 0)
	(while (< temp0 param1)
		(++ temp0)
		(if (& temp0 $0001) (continue))
		(+= temp1 temp0)
	)
	(return temp1)
)
