;;; Sierra Script 1.0 - (do not remove this comment)
(script# 969)
(include sci.sh)

(public
	r2RestBeforeTheLastArgument 0
	r2RestFirst 1
)

(procedure (r2RestBeforeTheLastArgument param1 param2 &tmp temp0)
	(param1 posn: &rest param2 temp0)
)

(procedure (r2RestFirst param1 param2)
	(Abs &rest param2 100)
)
