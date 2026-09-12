;;; Sierra Script 1.0 - (do not remove this comment)
(script# 20)
(include sci.sh)
(use Main)
(use Cycle)
(use Game)
(use Actor)
(use System)

(public
	deathRoom 0
)

(local
	[messageBuffer 200]
)
(procedure (localproc_0032)
	(iWannaQuit init:)
	(iWannaRestore init:)
	(iWannaRestart init:)
	(gUser canControl: 1 canInput: 1)
	(gIconBar enable:)
	(gIconBar enable: 2)
	(gIconBar select: (gIconBar at: 2))
	(gGame setCursor: 999)
)

(instance deathRoom of Rm
	(properties
		picture 200
	)
	
	(method (init)
		(Palette 4 0 255 100)
		(PalVary 3)
		(gGame handsOff:)
		(super init:)
		(gMusic2 stop:)
		(switch gDeathReason
			(else 
				(gRoom setScript: sGeneric)
			)
		)
	)
	
	(method (doVerb param1)
		(switch param1
			(else 
				(super doVerb: param1 &rest)
			)
		)
	)
)

(instance sGeneric of Script
	(properties)
	
	(method (changeState theState)
		(switch (= state theState)
			(0
				(gGame handsOff:)
				(Message 0 20 1 0 0 gDeathReason @messageBuffer)
				(Display
					@messageBuffer
					dsCOORD
					143
					68
					dsCOLOR
					0
					dsBACKGROUND
					5
					dsFONT
					1605
					dsWIDTH
					140
					dsALIGN
					1
				)
				(skull
					view: 2000
					loop: 0
					cel: 0
					init:
					posn: 42 101
					setCycle: Fwd
				)
				(= seconds 2)
			)
			(1
				(localproc_0032)
				(self dispose:)
			)
		)
	)
)

(instance skull of Prop
	(properties
		signal 16384
	)
	
	(method (doVerb param1)
		(switch param1
			(else 
				(super doVerb: param1 &rest)
			)
		)
	)
)

(instance iWannaRestart of View
	(properties
		x 50
		y 170
		view 2099
		loop 1
	)
	
	(method (doVerb param1)
		(switch param1
			(4
				(self cel: 1)
				(gGame restart:)
			)
			(else 
				(super doVerb: param1 &rest)
			)
		)
	)
)

(instance iWannaRestore of View
	(properties
		x 150
		y 170
		view 2099
	)
	
	(method (doVerb param1)
		(switch param1
			(4
				(self cel: 1)
				(gGame restore:)
			)
			(else 
				(super doVerb: param1 &rest)
			)
		)
	)
)

(instance iWannaQuit of View
	(properties
		x 250
		y 170
		view 2099
		loop 2
	)
	
	(method (doVerb param1)
		(switch param1
			(4
				(self cel: 1)
				(= gQuitGame 1)
			)
			(else 
				(super doVerb: param1 &rest)
			)
		)
	)
)
