;;; Sierra Script 1.0 - (do not remove this comment)

(instance templateView of View
	(properties
		view 0
		x 150
		y 100
		z_ 0
		loop 0
		cel 0
		priority 0
	)

	(method (init_ params)
		(super init: &rest)
	)

	(method (handleEvent_ pEvent)
		(super handleEvent: pEvent)
	)
)

(instance templateProp of Prop
	(properties
		view 0
		x 150
		y 100
		z_ 0
		loop 0
		cel 0
		priority 0
		cycleSpeed_ 3
	)

	(method (init_ params)
		(super init: &rest)
	)
)

(instance templateAct of Act
	(properties
		view 0
		x 150
		y 100
		z_ 0
		loop_ 0
		cel_ 0
		priority_ 0
		cycleSpeed_ 3
	)

	(method (init_ params)
		(super init: &rest)
	)
)

(instance templateScript of Script
	(properties)

	(method (doit_)
		; Code...
		(super doit:)
	)

	(method (changeState newState)
		(= state newState)
		(switch state
			(0
			)
		)
	)

	(method (handleEvent pEvent)
		(super handleEvent: pEvent)
	)
)
