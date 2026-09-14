;;; Sierra Script 1.0 - (do not remove this comment)

(class AllTheMethods of Obj
	(properties)

	(method (init params)
		(super init: &rest)
	)

	(method (handleEvent pEvent)
		(super handleEvent: pEvent)
	)

	(method (changeState newState)
		(= state newState)
		(switch state
			(0
			)
		)
	)

	(method (doit)
		(super doit:)
	)
)
