;;; Sierra Script 1.0 - (do not remove this comment)
(script# 999)
(include sci.sh)
(use Main)
(use Print)

(public
	Sign 0
	UModulo 1
	Min 2
	Max 3
	InRect 4
	IsOneOf 5
	WordAt 6
	Eval 7
)

(procedure (Sign param1)
	(return (if (< param1 0) -1 else (> param1 0)))
)

(procedure (UModulo param1 param2)
	(-= param1 (* param2 (/ param1 param2)))
	(if (< param1 0) (+= param1 param2))
	(return param1)
)

(procedure (Min param1 &tmp temp0)
	(return
		(if
		(or (== argc 1) (< param1 (= temp0 (Min &rest))))
			param1
		else
			temp0
		)
	)
)

(procedure (Max param1 &tmp temp0)
	(return
		(if
		(or (== argc 1) (> param1 (= temp0 (Max &rest))))
			param1
		else
			temp0
		)
	)
)

(procedure (InRect param1 param2 param3 param4 param5 param6)
	(return
		(if
			(and
				(<= param1 (if (< argc 6) (param5 x?) else param5))
				(<= (if (< argc 6) (param5 x?) else param5) param3)
			)
			(and
				(<= param2 (if (< argc 6) (param5 y?) else param6))
				(<= (if (< argc 6) (param5 y?) else param6) param4)
			)
		else
			0
		)
	)
)

(procedure (IsOneOf param1 param2 &tmp temp0)
	(= temp0 0)
	(while (< temp0 (- argc 1))
		(if (== param1 [param2 temp0])
			(return (or param1 1))
		else
			(++ temp0)
		)
	)
	(return 0)
)

(procedure (WordAt param1 param2)
	(Memory 5 (+ param1 (* 2 param2)))
)

(procedure (Eval param1 param2)
	(param1 param2: &rest)
)

(class Obj
	(properties)
	
	(method (new)
		(Clone self)
	)
	
	(method (init)
	)
	
	(method (doit)
		(return self)
	)
	
	(method (dispose)
		(DisposeClone self)
	)
	
	(method (showStr param1)
		(StrCpy param1 name)
	)
	
	(method (showSelf &tmp [temp0 200])
		(Prints (self showStr: @temp0))
	)
	
	(method (perform param1)
		(param1 doit: self &rest)
	)
	
	(method (isKindOf param1 &tmp obj_super_)
		(if
			(and
				(== -propDict- (param1 -propDict-?))
				(== -classScript- (param1 -classScript-?))
			)
			(return 1)
		)
		(= obj_super_ (self -super-?))
		(if (not obj_super_) (return 0))
		(if (IsObject obj_super_)
			(return (obj_super_ isKindOf: param1))
		)
		(return 0)
	)
	
	(method (isMemberOf param1)
		(if (== param1 self) (return 1))
		(if
			(and
				(& (param1 -info-?) $8000)
				(not (& -info- $8000))
			)
			(return (== -propDict- (param1 -propDict-?)))
		)
		(return 0)
	)
	
	(method (respondsTo param1)
		(RespondsTo self param1)
	)
	
	(method (yourself)
		(return self)
	)
)

(class Code of Obj
	(properties)
	
	(method (doit)
	)
)

(class Collect of Obj
	(properties
		elements 0
		size 0
	)
	
	(method (doit)
		(self eachElementDo: 57 &rest)
	)
	
	(method (dispose)
		(if elements
			(self eachElementDo: 111)
			(DisposeList elements)
		)
		(= size (= elements 0))
		(super dispose:)
	)
	
	(method (showStr param1)
		(Format param1 999 0 name size)
	)
	
	(method (showSelf &tmp [temp0 40])
		(Prints (self showStr: @temp0))
		(self eachElementDo: 113)
	)
	
	(method (add param1 &tmp temp0 temp1 temp2)
		(if (not elements) (= elements (NewList)))
		(= temp1 0)
		(while (< temp1 argc)
			(if (not (self isDuplicate: [param1 temp1]))
				(AddToEnd
					elements
					(NewNode [param1 temp1] [param1 temp1])
				)
				(++ size)
			)
			(++ temp1)
		)
		(return self)
	)
	
	(method (delete param1 &tmp temp0)
		(= temp0 0)
		(while (< temp0 argc)
			(if (DeleteKey elements [param1 temp0]) (-- size))
			(++ temp0)
		)
		(return self)
	)
	
	(method (eachElementDo param1 &tmp temp0 temp1 temp2)
		(= temp0 (FirstNode elements))
		(while temp0
			(= temp1 (NextNode temp0))
			(= temp2 (NodeValue temp0))
			(if (not (IsObject temp2))
				(return)
			else
				(temp2 param1: &rest)
				(= temp0 temp1)
			)
		)
	)
	
	(method (firstTrue param1 &tmp temp0 temp1 temp2)
		(= temp0 (FirstNode elements))
		(while temp0
			(= temp1 (NextNode temp0))
			(= temp2 (NodeValue temp0))
			(if (temp2 param1: &rest)
				(return temp2)
			else
				(= temp0 temp1)
			)
		)
		(return 0)
	)
	
	(method (allTrue param1 &tmp temp0 temp1 temp2)
		(= temp0 (FirstNode elements))
		(while temp0
			(= temp1 (NextNode temp0))
			(= temp2 (NodeValue temp0))
			(if (not (temp2 param1: &rest))
				(return 0)
			else
				(= temp0 temp1)
			)
		)
		(return 1)
	)
	
	(method (contains param1)
		(FindKey elements param1)
	)
	
	(method (isEmpty)
		(if (== elements 0) else (EmptyList elements))
	)
	
	(method (first)
		(FirstNode elements)
	)
	
	(method (next param1)
		(NextNode param1)
	)
	
	(method (release &tmp temp0 temp1)
		(= temp0 (FirstNode elements))
		(while temp0
			(= temp1 (NextNode temp0))
			(self delete: (NodeValue temp0))
			(= temp0 temp1)
		)
	)
	
	(method (isDuplicate)
		(return 0)
	)
)

(class List of Collect
	(properties
		elements 0
		size 0
	)
	
	(method (showStr param1)
		(Format param1 999 1 name size)
	)
	
	(method (at param1 &tmp temp0)
		(= temp0 (FirstNode elements))
		(while (and param1 temp0)
			(-- param1)
			(= temp0 (NextNode temp0))
		)
		(return (if temp0 (NodeValue temp0) else 0))
	)
	
	(method (last)
		(LastNode elements)
	)
	
	(method (prev param1)
		(PrevNode param1)
	)
	
	(method (addToFront param1 &tmp temp0)
		(if (not elements) (= elements (NewList)))
		(= temp0 (- argc 1))
		(while (<= 0 temp0)
			(if (not (self isDuplicate: [param1 temp0]))
				(AddToFront
					elements
					(NewNode [param1 temp0] [param1 temp0])
				)
				(++ size)
			)
			(-- temp0)
		)
		(return self)
	)
	
	(method (addToEnd param1 &tmp temp0)
		(if (not elements) (= elements (NewList)))
		(= temp0 0)
		(while (< temp0 argc)
			(if (not (self isDuplicate: [param1 temp0]))
				(AddToEnd
					elements
					(NewNode [param1 temp0] [param1 temp0])
				)
				(++ size)
			)
			(++ temp0)
		)
		(return self)
	)
	
	(method (addAfter param1 param2 &tmp temp0 temp1 temp2)
		(= temp2 (FindKey elements param1))
		(if temp2
			(-- argc)
			(= temp0 0)
			(while (< temp0 argc)
				(if (not (self isDuplicate: [param2 temp0]))
					(= temp2
						(AddAfter
							elements
							temp2
							(NewNode [param2 temp0] [param2 temp0])
						)
					)
					(++ size)
				)
				(++ temp0)
			)
		)
		(return self)
	)
	
	(method (indexOf param1 &tmp temp0 temp1)
		(= temp0 0)
		(= temp1 (FirstNode elements))
		(while temp1
			(if (== param1 (NodeValue temp1))
				(return temp0)
			else
				(++ temp0)
				(= temp1 (NextNode temp1))
			)
		)
		(return -1)
	)
)

(class Set of List
	(properties
		elements 0
		size 0
	)
	
	(method (showStr param1)
		(Format param1 999 2 name size)
	)
	
	(method (isDuplicate param1)
		(self contains: param1)
	)
)

(class EventHandler of Set
	(properties
		elements 0
		size 0
	)
	
	(method (handleEvent param1 &tmp temp0 temp1 temp2 temp3 temp4)
		(= temp3 (Clone param1))
		(= temp0 (FirstNode elements))
		(while (and temp0 (not (temp3 claimed?)))
			(= temp1 (NextNode temp0))
			(= temp2 (NodeValue temp0))
			(if (not (IsObject temp2))
				(break)
			else
				(temp2 handleEvent: temp3)
				(= temp0 temp1)
			)
		)
		(= temp4 (temp3 claimed?))
		(temp3 dispose:)
		(return temp4)
	)
)

(class Script of Obj
	(properties
		client 0
		state -1
		start 0
		timer 0
		cycles 0
		seconds 0
		lastSeconds 0
		ticks 0
		lastTicks 0
		register 0
		script 0
		caller 0
		next 0
	)
	
	(method (init theClient theCaller theRegister)
		(= lastTicks gGameTime)
		(if (>= argc 1)
			((= client theClient) script: self)
			(if (>= argc 2)
				(= caller theCaller)
				(if (>= argc 3) (= register theRegister))
			)
		)
		(= state (- start 1))
		(self cue:)
	)
	
	(method (doit &tmp theLastSeconds)
		(if script (script doit:))
		(cond 
			(cycles (if (not (-- cycles)) (self cue:)))
			(seconds
				(= theLastSeconds (GetTime 1))
				(if (!= lastSeconds theLastSeconds)
					(= lastSeconds theLastSeconds)
					(if (not (-- seconds)) (self cue:))
				)
			)
			(
				(and
					ticks
					(<= (-= ticks (Abs (- gGameTime lastTicks))) 0)
				)
				(= ticks 0)
				(self cue:)
			)
		)
		(= lastTicks gGameTime)
	)
	
	(method (dispose &tmp temp0)
		(if (IsObject script) (script dispose:))
		(if (IsObject timer) (timer dispose:))
		(if (IsObject client)
			(= temp0
				(cond 
					((IsObject next) next)
					(next (ScriptID next))
				)
			)
			(client script: temp0)
			(cond 
				((not temp0) 0)
				((== gNewRoomNumber gRoomNumber) (temp0 init: client))
				(else (temp0 dispose:))
			)
		)
		(if
		(and (IsObject caller) (== gNewRoomNumber gRoomNumber))
			(caller cue: register)
		)
		(= script (= timer (= client (= next (= caller 0)))))
		(super dispose:)
	)
	
	(method (changeState theState)
		(= state theState)
	)
	
	(method (cue)
		(if client (self changeState: (+ state 1) &rest))
	)
	
	(method (handleEvent param1)
		(if script (script handleEvent: param1))
		(param1 claimed?)
	)
	
	(method (setScript param1)
		(if (IsObject script) (script dispose:))
		(if param1 (param1 init: self &rest))
	)
)

(class Event of Obj
	(properties
		type 0
		message 0
		modifiers 0
		y 0
		x 0
		claimed 0
		port 0
	)
	
	(method (new param1 &tmp newSuper)
		(= newSuper (super new:))
		(GetEvent (if argc param1 else 32767) newSuper)
		(return newSuper)
	)
	
	(method (localize &tmp thePort)
		(if (not (& type $4000))
			(= thePort (GetPort))
			(cond 
				((not port) (GlobalToLocal self))
				((!= port thePort)
					(SetPort port)
					(LocalToGlobal self)
					(SetPort thePort)
					(GlobalToLocal self)
				)
			)
			(= port thePort)
		)
		(return self)
	)
	
	(method (globalize &tmp temp0)
		(if (not (& type $4000))
			(= temp0 (GetPort))
			(cond 
				((== port temp0) (LocalToGlobal self))
				(port (SetPort port) (LocalToGlobal self) (SetPort temp0))
			)
			(= port 0)
		)
		(return self)
	)
)

(class Cursor of Obj
	(properties
		view 0
		loop 0
		cel 0
		x 0
		y 0
		hotSpotX 0
		hotSpotY 0
		hidden 0
	)
	
	(method (init)
		(if (or hotSpotX hotSpotY)
			(SetCursor view loop cel hotSpotX hotSpotY)
		else
			(SetCursor view loop cel)
		)
	)
	
	(method (posn param1 param2)
		(SetCursor param1 param2)
	)
	
	(method (posnHotSpot theHotSpotX theHotSpotY)
		(= hotSpotX theHotSpotX)
		(= hotSpotY theHotSpotY)
		(self init:)
	)
	
	(method (setLoop theLoop)
		(= loop theLoop)
		(self init:)
	)
	
	(method (setCel theCel)
		(= cel theCel)
		(self init:)
	)
	
	(method (showCursor param1)
		(if argc (SetCursor param1))
	)
)
