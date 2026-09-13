;;; Sierra Script 1.0 - (do not remove this comment)
(script# 922)
(include sci.sh)

; A mid-body continue in a loop. The extra jump to the loop head creates a
; second back edge and a common latch; the structurer must recognize the
; mid-body jump as a continue, not part of the natural back edge.
(public
	f10MidBodyContinue 0
)

(procedure (f10MidBodyContinue param1 &tmp temp0 temp1)
	(= temp0 0)
	(= temp1 0)
	(while (< temp0 param1)
		(++ temp0)
		(if (& temp0 1)
			(continue)
		)
		(= temp1 (+ temp1 temp0))
	)
	(return temp1)
)
