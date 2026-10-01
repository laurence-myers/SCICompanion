;;; Sierra Script 1.0 - (do not remove this comment)
(script# 970)
(include sci.sh)
(use System)

(public
	c5RestWithACallInTheTarget 0
)

(procedure (c5RestWithACallInTheTarget param1)
	((ScriptID param1) init: &rest)
)

(instance c5Script of Script
	(properties)
	
	(method (doit)
		(self state: 1 2)
	)
)
