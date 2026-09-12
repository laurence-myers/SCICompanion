;;; Sierra Script 1.0 - (do not remove this comment)
(script# 995)
(include sci.sh)
(use Main)
(use Print)
(use IconItem)


(class InventoryItem of IconItem
	(properties
		view 0
		loop 0
		cel 0
		nsLeft 0
		nsTop 0
		nsRight 0
		nsBottom 0
		state 0
		cursor 999
		type 16384
		message 0
		modifiers 0
		signal 0
		maskView 0
		maskLoop 0
		maskCel 0
		highlightColor 0
		lowlightColor 0
		noun 0
		modNum 0
		helpVerb 0
		owner 0
		script 0
		value 0
	)
	
	(method (show &tmp [temp0 4])
		(DrawCel view loop cel nsLeft nsTop -1)
	)
	
	(method (highlight param1 &tmp temp0 temp1 temp2 temp3 temp4)
		(if (== highlightColor -1) (return))
		(= temp4
			(if (and argc param1) highlightColor else lowlightColor)
		)
		(= temp0 (- nsTop 2))
		(= temp1 (- nsLeft 2))
		(= temp2 (+ nsBottom 1))
		(= temp3 (+ nsRight 1))
		(Graph 4 temp0 temp1 temp0 temp3 temp4 -1 -1)
		(Graph 4 temp0 temp3 temp2 temp3 temp4 -1 -1)
		(Graph 4 temp2 temp3 temp2 temp1 temp4 -1 -1)
		(Graph 4 temp2 temp1 temp0 temp1 temp4 -1 -1)
		(Graph
			12
			(- nsTop 2)
			(- nsLeft 2)
			(+ nsBottom 2)
			(+ nsRight 2)
			1
		)
	)
	
	(method (onMe param1)
		(return (and (super onMe: param1) (not (& signal $0004))))
	)
	
	(method (ownedBy param1)
		(return (== owner param1))
	)
	
	(method (moveTo theOwner)
		(= owner theOwner)
		(if (and value (== theOwner gEgo))
			(AddToScore value)
			(= value 0)
		)
		(return self)
	)
	
	(method (doVerb param1 &tmp temp0 temp1)
		(if (not modNum) (= modNum gRoomNumber))
		(switch param1
			(1
				(if (Message 2 modNum noun 1 0 1)
					(= temp1 (CelWide view loop cel))
					(= temp0 (GetPort))
					(Print
						addIcon: view loop cel 0 0
						addText: noun 1 0 1 (+ temp1 4) 0 modNum
						init:
					)
					(SetPort temp0)
				)
			)
			(4
				(if (Message 2 modNum noun 4 0 1)
					(= temp0 (GetPort))
					(Print addText: noun 4 0 0 0 0 modNum init:)
					(SetPort temp0)
				else
					(= temp0 (GetPort))
					(Print addText: 0 4 0 0 0 0 modNum init:)
					(SetPort temp0)
				)
			)
			(else 
				(if (Message 2 modNum noun param1 0 1)
					(= temp0 (GetPort))
					(Print addText: noun param1 0 0 0 0 modNum init:)
					(SetPort temp0)
				else
					(= temp0 (GetPort))
					(Print addText: 0 7 0 0 0 0 modNum init:)
					(SetPort temp0)
				)
			)
		)
	)
)
