;;; Sierra Script 1.0 - (do not remove this comment)
(script# 927)
(include sci.sh)

(public
	r1IfReturns 0
	r1ValueIf 1
	r1Increment 2
)

(procedure (r1IfReturns param1)
	(if param1 (return 1) else (return 0))
)

(procedure (r1ValueIf param1 param2)
	(return (if param1 1 else (param2 init:)))
)

(procedure (r1Increment param1 param2 &tmp temp0)
	(if param1 (param2 init:) else (++ temp0))
)
