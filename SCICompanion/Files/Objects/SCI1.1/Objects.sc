;;; Sierra Script 1.0 - (do not remove this comment)

(instance templateFeature of Feature
	(properties
		x 150
		y 100
		onMeCheck omcCOLORS
		noun N_NOUN
		nsTop_ 0
		nsLeft_ 0
		nsBottom_ 190
		nsRight_ 320
		sightAngle_ 40
		approachX_ 130
		approachY_ 120
		approachDist_ 10
	)

	(method (init_ params)
		(super init: &rest)
	)

	(method (doVerb_ theVerb params)
		(switch theVerb
			(else
				(super doVerb: theVerb &rest)
			)
		)
	)

	(method (handleEvent_ pEvent)
		(super handleEvent: pEvent)
	)
)

(instance templateView of View
	(properties
		view 0
		x 150
		y 100
		signal ignAct
		z_ 0
		loop 0
		cel 0
		noun N_NOUN
		nsTop_ 0
		nsLeft_ 0
		nsBottom_ 190
		nsRight_ 320
		sightAngle_ 40
		approachX_ 130
		approachY_ 120
		approachDist_ 10
		priority 0
	)

	(method (init_ params)
		(super init: &rest)
	)

	(method (doVerb_ theVerb params)
		(switch theVerb
			(else
				(super doVerb: theVerb &rest)
			)
		)
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
		signal ignAct
		z_ 0
		loop 0
		cel 0
		noun N_NOUN
		nsTop_ 0
		nsLeft_ 0
		nsBottom_ 190
		nsRight_ 320
		sightAngle_ 40
		approachX_ 130
		approachY_ 120
		approachDist_ 10
		priority 0
		cycleSpeed_ 3
	)

	(method (init_ params)
		(super init: &rest)
	)

	(method (doVerb_ theVerb params)
		(switch theVerb
			(else
				(super doVerb: theVerb &rest)
			)
		)
	)

	(method (handleEvent_ pEvent)
		(super handleEvent: pEvent)
	)
)

(instance templateActor of Actor
	(properties
		view 0
		x 150
		y 100
		signal ignAct
		z_ 0
		loop_ 0
		cel_ 0
		noun N_NOUN
		nsTop_ 0
		nsLeft_ 0
		nsBottom_ 190
		nsRight_ 320
		sightAngle_ 40
		approachX_ 130
		approachY_ 120
		approachDist_ 10
		priority_ 0
		cycleSpeed_ 3
	)

	(method (init_ params)
		(super init: &rest)
	)

	(method (doVerb_ theVerb params)
		(switch theVerb
			(else
				(super doVerb: theVerb &rest)
			)
		)
	)

	(method (handleEvent_ pEvent)
		(super handleEvent: pEvent)
	)
)

(instance templateScript of Script
	(properties)

	(method (doit_)
		; This code gets called on every update.
		(super doit:)
	)

	(method (changeState newState)
		(= state newState)
		(switch state
			(0
			)
		)
	)

	(method (handleEvent_ pEvent)
		(super handleEvent: pEvent)
	)
)

(instance templateTalker of Talker
	(properties
		x 150
		y 100
		view 100
		loop 0
		talkWidth 150
		back 5
		textX 120
		textY 10
	)

	(method (init params)
		(= font gFont)
		(super init: theBustProp theEyesProp theMouthProp &rest)
	)
)

(instance templateNarrator of Narrator
	(properties
		talkWidth 120
	)

	(method (init params)
		(= font gFont)
		(= gWindow SpeakWindow)
		(gWindow
			tailX: 85
			tailY: 135
			xOffset: 15
		)
		(super init: &rest)
	)

	(method (dispose param1)
		(= gWindow gWindow2)
		(super dispose: &rest)
	)
)
