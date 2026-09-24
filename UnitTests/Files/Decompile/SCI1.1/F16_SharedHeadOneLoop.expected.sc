;;; Sierra Script 1.0 - (do not remove this comment)
(script# 938)
(include sci.sh)

(public
	f16SharedHeadOneLoop 0
)

(procedure (f16SharedHeadOneLoop &tmp temp0 temp1)
	(repeat
		(cond 
			((< temp0 10) (++ temp0))
			((> temp1 5) (break))
			(else (++ temp1))
		)
	)
)
