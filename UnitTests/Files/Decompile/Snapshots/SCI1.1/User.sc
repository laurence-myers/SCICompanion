;;; Sierra Script 1.0 - (do not remove this comment)
(script# 996)
(include sci.sh)
(use Main)
(use System)


(class User of Obj
	(properties
		alterEgo 0
		input 0
		controls 0
		prevDir 0
		x -1
		y -1
		mapKeyToDir 1
		curEvent 0
	)
	
	(method (init)
		(= curEvent uEvt)
	)
	
	(method (doit)
		(curEvent new:)
		(self handleEvent: curEvent)
	)
	
	(method (canControl theControls)
		(if argc (= controls theControls) (= prevDir 0))
		(return controls)
	)
	
	(method (handleEvent theGPEvent &tmp theGPEventType theGPEventMessage theGPEventModifiers temp3 temp4)
		(= gPEventX (theGPEvent x?))
		(= gPEventY (theGPEvent y?))
		(= theGPEventType (theGPEvent type?))
		(= theGPEventModifiers (theGPEvent modifiers?))
		(if theGPEventType
			(= gPEvent theGPEvent)
			(if mapKeyToDir (MapKeyToDir theGPEvent))
			(if (== theGPEventType 256)
				(= theGPEventType 4)
				(= theGPEventMessage
					(if (& theGPEventModifiers $0003) 27 else 13)
				)
				(= theGPEventModifiers 0)
				(theGPEvent
					type: theGPEventType
					message: theGPEventMessage
					modifiers: theGPEventModifiers
				)
			)
			(if (and gPrints (gPrints handleEvent: theGPEvent))
				(return 1)
			)
			(theGPEvent localize:)
			(= theGPEventType (theGPEvent type?))
			(= theGPEventMessage (theGPEvent message?))
			(cond 
				((& theGPEventType $0080)
					(if
						(and
							(== theGPEventMessage 1)
							(or
								(= temp4 (gCast firstTrue: 96 findNoun))
								(= temp4 (gFeatures firstTrue: 96 findNoun))
								(= temp4 (gAddToPics firstTrue: 96 findNoun))
							)
						)
						(temp4 doVerb: ((gIconBar curIcon?) message?))
					else
						(= temp4 (gIconBar findIcon: theGPEventMessage))
						(if temp4
							(gIconBar
								select: (= temp4 (gIconBar findIcon: theGPEventMessage))
							)
							(gGame setCursor: (temp4 cursor?))
						)
					)
				)
				((& theGPEventType $0040)
					(cond 
						((and gOldDH (gOldDH handleEvent: theGPEvent)) (return 1))
						(
							(and
								(or
									(and
										gIconBar
										(== (gIconBar curIcon?) (gIconBar walkIconItem?))
									)
									(not gIconBar)
								)
								alterEgo
								controls
								(gCast contains: alterEgo)
								(alterEgo handleEvent: theGPEvent)
							)
							(return 1)
						)
						(
							(and
								gPseudoMouse
								(or
									(not (& theGPEventType $0004))
									(!= theGPEventMessage 0)
								)
								(gPseudoMouse handleEvent: theGPEvent)
							)
							(return 1)
						)
					)
				)
				(
					(and
						(& theGPEventType $0004)
						gOldKH
						(gOldKH handleEvent: theGPEvent)
					)
					(return 1)
				)
				(
					(and
						(& theGPEventType $0003)
						gOldMH
						(gOldMH handleEvent: theGPEvent)
					)
					(return 1)
				)
			)
		)
		(if gIconBar (gIconBar handleEvent: theGPEvent))
		(= theGPEventType (theGPEvent type?))
		(= theGPEventMessage (theGPEvent message?))
		(if (and input (& theGPEventType $4000))
			(cond 
				(
					(and
						(& theGPEventType $1000)
						gWalkHandler
						(gWalkHandler handleEvent: theGPEvent)
					)
					(return 1)
				)
				(
					(and
						(& theGPEventType $1000)
						(gCast contains: alterEgo)
						controls
						(alterEgo handleEvent: theGPEvent)
					)
					(return 1)
				)
				(gUseSortedFeatures
					(OnMeAndLowY init:)
					(gCast eachElementDo: 96 OnMeAndLowY theGPEvent)
					(gFeatures eachElementDo: 96 OnMeAndLowY theGPEvent)
					(gAddToPics eachElementDo: 96 OnMeAndLowY theGPEvent)
					(if
						(and
							(OnMeAndLowY theObj?)
							((OnMeAndLowY theObj?) handleEvent: theGPEvent)
						)
						(return 1)
					)
				)
				((gCast handleEvent: theGPEvent) (return 1))
				((gFeatures handleEvent: theGPEvent) (return 1))
				((gAddToPics handleEvent: theGPEvent) (return 1))
			)
			(if
				(and
					(not (theGPEvent claimed?))
					(gRegions handleEvent: theGPEvent)
				)
				(return 1)
			)
		)
		(if theGPEventType
			(cond 
				((gGame handleEvent: theGPEvent) (return 1))
				((and gPrints (gPrints handleEvent: theGPEvent)) (return 1))
			)
		)
		(return 0)
	)
	
	(method (canInput theInput)
		(if argc (= input theInput))
		(return input)
	)
)

(class OnMeAndLowY of Code
	(properties
		theObj 0
		lastY -1
	)
	
	(method (init)
		(= theObj 0)
		(= lastY -1)
	)
	
	(method (doit theTheObj param2)
		(if
		(and (theTheObj onMe: param2) (> (theTheObj y?) lastY))
			(= lastY ((= theObj theTheObj) y?))
		)
	)
)

(instance uEvt of Event
	(properties)
	
	(method (new)
		(= type
			(= message
				(= modifiers (= y (= x (= claimed (= port 0)))))
			)
		)
		(GetEvent 32767 self)
		(return self)
	)
)

(instance findNoun of Code
	(properties)
	
	(method (doit param1 param2)
		(return (== (param1 noun?) param2))
	)
)
