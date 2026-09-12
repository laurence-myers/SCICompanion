;;; Sierra Script 1.0 - (do not remove this comment)
(script# 943)
(include sci.sh)
(use Main)
(use Print)
(use Polygon)
(use SysWindow)
(use File)
(use System)


(local
	local0
	theMainWin
	[local2 40]
	theLsTop
	theLsLeft
	theLsBottom
	theLsRight
	[local46 25] = [{EDITING} 0 0 {About} 0 0 {Map} 0 0 {Create} 0 0 {Type} 0 0 {Undo} 0 0 {Help} 0 0 {eXit} 120]
	[local71 22] = [{CREATING} 0 0 {About} 0 0 {Map} 0 0 {Done} 0 0 {Undo} 0 0 {Help} 0 0 {eXit} 120]
)
(procedure (localproc_0184 theTheTheLsLeft &tmp temp0 theTheLsLeft theTheLsTop)
	(= theLsBottom 0)
	(= theLsRight theLsBottom)
	(= theLsTop 32767)
	(= theLsLeft theLsTop)
	(= temp0 0)
	(while (< temp0 argc)
		(= theTheLsLeft [theTheTheLsLeft temp0])
		(= theTheLsTop [theTheTheLsLeft (+ temp0 1)])
		(if (< theTheLsTop theLsTop) (= theLsTop theTheLsTop))
		(if (> theTheLsTop theLsBottom)
			(= theLsBottom theTheLsTop)
		)
		(if (< theTheLsLeft theLsLeft)
			(= theLsLeft theTheLsLeft)
		)
		(if (> theTheLsLeft theLsRight)
			(= theLsRight theTheLsLeft)
		)
		(= temp0 (+ temp0 2))
	)
	(= theLsLeft (- theLsLeft 2))
	(= theLsTop (- theLsTop 2))
	(= theLsRight (+ theLsRight 2))
	(= theLsBottom (+ theLsBottom 2))
)

(procedure (localproc_0203 param1 param2 param3 param4)
	(return
		(+
			(* (+ (/ param1 2) 1) (+ (/ param3 2) 1))
			(* (+ (/ param2 2) 1) (+ (/ param4 2) 1))
		)
	)
)

(procedure (localproc_022e param1 param2 param3 param4 param5 param6 &tmp temp0)
	(return
		(if
			(and
				(<=
					0
					(localproc_0203
						(- param3 param1)
						(- param4 param2)
						(- param5 param1)
						(- param6 param2)
					)
				)
				(<=
					0
					(localproc_0203
						(- param1 param3)
						(- param2 param4)
						(- param5 param3)
						(- param6 param4)
					)
				)
			)
			(= temp0 (GetDistance param1 param2 param3 param4))
			(return
				(if temp0
					(/
						(Abs
							(localproc_0203
								(- param4 param2)
								(- param1 param3)
								(- param5 param1)
								(- param6 param2)
							)
						)
						temp0
					)
				else
					0
				)
			)
		else
			(return
				(Min
					(GetDistance param5 param6 param1 param2)
					(GetDistance param5 param6 param3 param4)
				)
			)
		)
	)
)

(procedure (localproc_02d8 param1 param2)
	(Print
		width: 240
		font: 999
		mode: param2
		addText: param1
		addTitle: {Polygon Editor 1.11}
		init:
	)
)

(class ClickMenu of Obj
	(properties
		text 0
		array 0
	)
	
	(method (init theArray &tmp temp0 temp1 temp2 temp3 [temp4 3] temp7 [temp8 40] temp48 temp49)
		(= text (Memory 2 81))
		(Memory 6 text 0)
		(= temp1 (= array theArray))
		(= temp48 0)
		(= temp0 0)
		(while (= temp2 (Memory 5 temp1))
			(StrCpy @temp8 temp2)
			(if (not temp0) (StrCat @temp8 {:_}))
			(StrCat @temp8 {_})
			(StrCat text @temp8)
			(TextSize @temp4 @temp8 0 0)
			(= temp48 (+ temp48 temp7))
			(Memory 6 (+ temp1 4) temp48)
			(if (not (Memory 5 (+ temp1 2)))
				(= temp49 (StrAt temp2 0))
				(if
					(and
						(<= 65 temp49)
						(<= (= temp49 (StrAt temp2 0)) 90)
					)
					(= temp49 (+ temp49 32))
				)
				(Memory 6 (+ temp1 2) temp49)
			)
			(++ temp0)
			(= temp1 (+ temp1 6))
		)
		(DrawStatus text)
	)
	
	(method (dispose)
		(Memory 3 text)
		(super dispose:)
	)
	
	(method (handleEvent param1 &tmp theArray temp1)
		(if (!= (param1 type?) 1) (return 0))
		(if (>= (param1 y?) 10) (return 0))
		(= theArray array)
		(= temp1 0)
		(while (Memory 5 theArray)
			(if
			(and (< (param1 x?) (Memory 5 (+ theArray 4))) temp1)
				(param1 type: 4 message: (Memory 5 (+ theArray 2)))
				(return 0)
			)
			(++ temp1)
			(= theArray (+ theArray 6))
		)
		(return (param1 claimed: 1))
	)
)

(class Class_943_3
	(properties
		x 0
		y 0
		underBits 0
	)
	
	(method (new)
		(Clone self)
	)
	
	(method (yourself)
		(return self)
	)
	
	(method (dispose)
		(if underBits (UnLoad 133 underBits) (= underBits 0))
		(DisposeClone self)
	)
	
	(method (draw param1 param2 &tmp temp0 temp1)
		(if (== local0 4)
			(= temp0 -1)
			(= temp1 (if (== param2 1) 3 else 4))
		else
			(= temp0 0)
			(= temp1 -1)
		)
		(Graph 4 y x (param1 y?) (param1 x?) temp0 -1 temp1)
	)
	
	(method (save param1)
		(localproc_0184 x y (param1 x?) (param1 y?))
		(if underBits (UnLoad 133 underBits))
		(= underBits
			(Graph 7 theLsTop theLsLeft theLsBottom theLsRight local0)
		)
	)
	
	(method (restore)
		(if underBits (Graph 8 underBits) (= underBits 0))
	)
)

(class _EditablePolygon of List
	(properties
		elements 0
		size 0
		curNode 0
		curPt 0
		closed 0
		type 2
		srcList 0
		closestPt 0
		lsTop 0
		lsLeft 0
		lsBottom 0
		lsRight 0
	)
	
	(method (add param1 param2 param3)
		(super
			add: (= curPt
				((Class_943_3 new:) x: param1 y: param2 yourself:)
			)
		)
		(self setCur: (FindKey elements curPt) param3)
	)
	
	(method (next param1 &tmp temp0)
		(= temp0 (super next: param1))
		(if (and closed (not temp0)) (return (super first:)))
		(return temp0)
	)
	
	(method (prev param1 &tmp temp0)
		(= temp0 (super prev: param1))
		(if (and closed (not temp0)) (return (super last:)))
		(return temp0)
	)
	
	(method (draw)
		(self eachLineDo: 80 srcList)
	)
	
	(method (advance &tmp temp0)
		(self setCur: (self next: curNode))
	)
	
	(method (retreat &tmp temp0)
		(self setCur: (self prev: curNode))
	)
	
	(method (setCur theCurNode param2)
		(if (= curNode theCurNode)
			(= curPt (NodeValue (= curNode theCurNode)))
			(if (or (< argc 2) param2)
				(gGame setCursor: 999 1 (curPt x?) (curPt y?))
			)
		)
	)
	
	(method (setCurClosest param1)
		(self setCur: (FindKey elements closestPt) param1)
	)
	
	(method (insertPt param1 param2 &tmp temp0)
		(= temp0
			((Class_943_3 new:) x: param1 y: param2 yourself:)
		)
		(self addAfter: closestPt temp0)
		(self setCur: (FindKey elements temp0))
	)
	
	(method (deletePt &tmp temp0)
		(= temp0 (self prev: curNode))
		(if (== curNode temp0) (= temp0 0))
		(self delete: curPt)
		(curPt dispose:)
		(self setCur: temp0)
	)
	
	(method (movePt param1 param2)
		(curPt x: param1 y: param2)
	)
	
	(method (undo param1 &tmp temp0 temp1 temp2 temp3)
		(self eachElementDo: 111 release:)
		(= closed (Memory 5 param1))
		(= param1 (+ param1 2))
		(= temp1 (Memory 5 param1))
		(= param1 (+ param1 2))
		(= temp3 (Memory 5 param1))
		(= param1 (+ param1 2))
		(= temp2 0)
		(while (< temp2 temp1)
			(= param1 (+ param1 2))
			(self add: (Memory 5 param1) (Memory 5 param1) 0)
			(++ temp2)
			(= param1 (+ param1 2))
		)
		(self setCur: (FindKey elements (self at: temp3)) 0)
	)
	
	(method (saveForUndo &tmp temp0 temp1 _EditablePolygonFirst temp3)
		(= temp0 (Memory 2 (* 2 (+ (* 2 size) 3))))
		(= temp1 temp0)
		(Memory 6 temp1 closed)
		(= temp1 (+ temp1 2))
		(Memory 6 temp1 size)
		(= temp1 (+ temp1 2))
		(Memory 6 temp1 (self indexOf: curPt))
		(= temp1 (+ temp1 2))
		(= _EditablePolygonFirst (self first:))
		(while _EditablePolygonFirst
			(= temp3 (NodeValue _EditablePolygonFirst))
			(Memory 6 temp1 (temp3 x?))
			(= temp1 (+ temp1 2))
			(Memory 6 temp1 (temp3 y?))
			(= _EditablePolygonFirst
				(NextNode _EditablePolygonFirst)
			)
			(= temp1 (+ temp1 2))
		)
		(return temp0)
	)
	
	(method (getDistToLine param1 param2 &tmp _EditablePolygonLast _EditablePolygonFirst theClosestPt temp3 temp4 temp5 the_EditablePolygonFirst)
		(if (< size 2)
			(return (self getDistToPt: param1 param2 &rest))
		)
		(= temp4 32767)
		(= _EditablePolygonFirst (self first:))
		(= _EditablePolygonLast (self last:))
		(repeat
			(= theClosestPt (NodeValue _EditablePolygonFirst))
			(= the_EditablePolygonFirst
				(self next: _EditablePolygonFirst)
			)
			(= temp5 (NodeValue the_EditablePolygonFirst))
			(= temp3
				(localproc_022e
					(theClosestPt x?)
					(theClosestPt y?)
					(temp5 x?)
					(temp5 y?)
					param1
					param2
				)
			)
			(if (< temp3 temp4)
				(= temp4 temp3)
				(= closestPt theClosestPt)
			)
			(if (== _EditablePolygonFirst _EditablePolygonLast)
				(break)
			)
			(= _EditablePolygonFirst the_EditablePolygonFirst)
		)
		(return temp4)
	)
	
	(method (getDistToPt param1 param2 &tmp _EditablePolygonLast _EditablePolygonFirst theClosestPt temp3 temp4)
		(= temp4 32767)
		(= _EditablePolygonFirst (self first:))
		(= _EditablePolygonLast (self last:))
		(repeat
			(= theClosestPt (NodeValue _EditablePolygonFirst))
			(= temp3
				(GetDistance
					param1
					param2
					(theClosestPt x?)
					(theClosestPt y?)
				)
			)
			(if (< temp3 temp4)
				(= temp4 temp3)
				(= closestPt theClosestPt)
			)
			(if (== _EditablePolygonFirst _EditablePolygonLast)
				(break)
			)
			(= _EditablePolygonFirst
				(self next: _EditablePolygonFirst)
			)
		)
		(return temp4)
	)
	
	(method (startRedraw &tmp temp0 temp1 temp2 theCurPt theCurPt_2)
		(= temp0 (self next: curNode))
		(if temp0
			(= theCurPt (NodeValue (= temp0 (self next: curNode))))
		else
			(= theCurPt curPt)
		)
		(= temp1 (self prev: curNode))
		(if temp1
			(= theCurPt_2
				(NodeValue (= temp1 (self prev: curNode)))
			)
		else
			(= theCurPt_2 curPt)
		)
		(localproc_0184
			(theCurPt_2 x?)
			(theCurPt_2 y?)
			(curPt x?)
			(curPt y?)
			(theCurPt x?)
			(theCurPt y?)
		)
		(= lsTop theLsTop)
		(= lsLeft theLsLeft)
		(= lsBottom theLsBottom)
		(= lsRight theLsRight)
	)
	
	(method (endRedraw)
		(localproc_0184
			(curPt x?)
			(curPt y?)
			lsLeft
			lsTop
			lsRight
			lsBottom
		)
		(Graph
			12
			theLsTop
			theLsLeft
			theLsBottom
			theLsRight
			local0
		)
	)
	
	(method (restore)
		(self eachLineDo: 76)
	)
	
	(method (save)
		(self eachLineDo: 75)
	)
	
	(method (eachLineDo param1 &tmp _EditablePolygonFirst the_EditablePolygonFirst temp2 temp3 _EditablePolygonLast)
		(= _EditablePolygonFirst (self first:))
		(= _EditablePolygonLast (self last:))
		(while
			(or
				(!= _EditablePolygonFirst _EditablePolygonLast)
				closed
			)
			(= the_EditablePolygonFirst
				(self next: _EditablePolygonFirst)
			)
			(= temp2 (NodeValue _EditablePolygonFirst))
			(= temp3 (NodeValue the_EditablePolygonFirst))
			(temp2 param1: temp3 &rest)
			(if (== _EditablePolygonFirst _EditablePolygonLast)
				(break)
			)
			(= _EditablePolygonFirst the_EditablePolygonFirst)
		)
	)
	
	(method (writeObstacle &tmp temp0 temp1 temp2 temp3 temp4)
		(= temp1 (Memory 2 (* size 4)))
		(= temp2 (FirstNode elements))
		(= temp0 temp1)
		(while temp2
			(= temp3 (NodeValue temp2))
			(Memory 6 temp0 (temp3 x?))
			(Memory 6 (+ temp0 2) (temp3 y?))
			(= temp2 (NextNode temp2))
			(= temp0 (+ temp0 4))
		)
		(if (== srcList 1)
			(gAltPolyList
				add:
					((Polygon new:)
						type: type
						points: temp1
						size: size
						dynamic: 1
						yourself:
					)
			)
		else
			(gRoom
				addObstacle:
					((Polygon new:)
						type: type
						points: temp1
						size: size
						dynamic: 1
						yourself:
					)
			)
		)
	)
	
	(method (writeFile param1 param2 &tmp temp0 temp1 temp2 temp3 [temp4 10] temp14)
		(if (== param2 srcList)
			(param1
				writeString:
					{\t\t\t((Polygon new:)\n\n}
					{\t\t\t\ttype:\t\t}
					(switch type
						(0 {PTotalAccess})
						(1 {PNearestAccess})
						(2 {PBarredAccess})
						(3 {PContainedAccess})
					)
					{,\n\n}
			)
			(param1 writeString: {\t\t\t\tinit:\t\t})
			(= temp14 1)
			(= temp0 17)
			(= temp3 (FirstNode elements))
			(while temp3
				(= temp2 (NodeValue temp3))
				(Format @temp4 943 0 (temp2 x?) (temp2 y?))
				(= temp1 (+ (StrLen @temp4) 1))
				(= temp0 (+ temp0 temp1))
				(if (>= temp0 80)
					(param1 writeString: {\n\n\t\t\t\t\t\t})
					(= temp14 1)
					(= temp0 (+ 17 temp1))
				)
				(if (not temp14) (param1 writeString: {_}))
				(param1 writeString: @temp4)
				(= temp14 0)
				(= temp3 (NextNode temp3))
			)
			(param1 writeString: {,\n\n})
			(param1
				writeString: {\t\t\t\tyourself:\n\n} {\t\t\t)\n\n}
			)
		)
	)
	
	(method (getAccessType &tmp temp0)
		(= temp0
			(Print
				addTitle: {Polygon access type}
				addButton: 1 {Total} 0 0
				addButton: 2 { Near_} 60 0
				addButton: 3 { Barred_} 120 0
				addButton: 4 { Container_} 195 0
				first: type
				init:
			)
		)
		(if temp0
			(= type
				(-
					(= temp0
						(Print
							addTitle: {Polygon access type}
							addButton: 1 {Total} 0 0
							addButton: 2 { Near_} 60 0
							addButton: 3 { Barred_} 120 0
							addButton: 4 { Container_} 195 0
							first: type
							init:
						)
					)
					1
				)
			)
		)
	)
	
	(method (check &tmp _EditablePolygonFirst the_EditablePolygonFirst temp2 temp3 temp4 temp5 _EditablePolygonFirst_2 temp7 temp8 temp9 _EditablePolygonLast temp11 temp12 temp13 temp14 temp15 temp16 [temp17 40])
		(= _EditablePolygonFirst (self first:))
		(while _EditablePolygonFirst
			(= temp2 (NodeValue _EditablePolygonFirst))
			(= temp15 (NextNode _EditablePolygonFirst))
			(while temp15
				(= temp16 (NodeValue temp15))
				(if
					(and
						(== (temp2 x?) (temp16 x?))
						(== (temp2 y?) (temp16 y?))
					)
					(= temp15 (PrevNode temp15))
					(self delete: temp16)
					(temp16 dispose:)
				)
				(= temp15 (NextNode temp15))
			)
			(= _EditablePolygonFirst
				(NextNode _EditablePolygonFirst)
			)
		)
		(= temp4 0)
		(= temp9 0)
		(= temp7 0)
		(= temp8 1)
		(= _EditablePolygonFirst
			(= _EditablePolygonFirst_2 (self first:))
		)
		(repeat
			(= temp2 (NodeValue _EditablePolygonFirst))
			(= the_EditablePolygonFirst
				(self next: _EditablePolygonFirst)
			)
			(= temp3 (NodeValue the_EditablePolygonFirst))
			(= temp4
				(ATan (temp2 x?) (temp2 y?) (temp3 x?) (temp3 y?))
			)
			(if (not temp8)
				(cond 
					((> (= temp5 (- temp4 temp9)) 180) (= temp5 (- temp5 360)))
					((< temp5 -180) (= temp5 (+ temp5 360)))
				)
				(= temp7 (+ temp7 temp5))
			)
			(= temp9 temp4)
			(if
			(== _EditablePolygonFirst _EditablePolygonFirst_2)
				(not temp8)
			)
			(= temp8 0)
			(= _EditablePolygonFirst the_EditablePolygonFirst)
		)
		(if (== type 3) (= temp7 (- temp7)))
		(cond 
			((== temp7 -360)
				(= _EditablePolygonFirst (self first:))
				(= _EditablePolygonLast (self last:))
				(while
					(and
						(!= _EditablePolygonFirst _EditablePolygonLast)
						(!=
							_EditablePolygonFirst
							(NextNode _EditablePolygonLast)
						)
					)
					(= temp2 (NodeValue _EditablePolygonFirst))
					(= temp11 (NodeValue _EditablePolygonLast))
					(= temp12 (temp2 x?))
					(= temp13 (temp2 y?))
					(temp2 x: (temp11 x?))
					(temp2 y: (temp11 y?))
					(temp11 x: temp12)
					(temp11 y: temp13)
					(= _EditablePolygonFirst
						(NextNode _EditablePolygonFirst)
					)
					(= _EditablePolygonLast (PrevNode _EditablePolygonLast))
				)
			)
			((!= temp7 360) (Format @temp17 943 1 name temp7) (Prints @temp17))
		)
	)
)

(class PolyEdit of List
	(properties
		elements 0
		size 0
		curPolygon 0
		x 0
		y 0
		state 0
		isMouseDown 0
		curMenu 0
		undoPrvPoly 0
		undoPoly 0
		undoPolyBuf 0
		undoX 0
		undoY 0
		undoState 0
	)
	
	(method (init)
		(DrawPic (gRoom curPic?) 100 1)
		(if (!= gPicNumber -1) (DrawPic gPicNumber 100 0))
		(gAddToPics doit:)
		(gCast eachElementDo: 299)
		(Animate (gCast elements?) 0)
		(= theMainWin gWindow)
		(= gWindow SysWindow)
		(= local0 1)
		(gGame setCursor: 999 1)
		(self readObstacles:)
		(self changeState: (if size 1 else 0))
		(self draw:)
	)
	
	(method (doit &tmp newEvent [temp1 100])
		(self init:)
		(repeat
			(= newEvent (Event new:))
			(if
			(not (if curMenu (curMenu handleEvent: newEvent)))
				(GlobalToLocal newEvent)
				(if (self handleEvent: newEvent) (break))
			)
			(newEvent dispose:)
		)
		(newEvent dispose:)
		(self dispose:)
	)
	
	(method (dispose)
		(self writeObstacles:)
		(if curMenu (curMenu dispose:) (= curMenu 0))
		(if undoPolyBuf
			(Memory 3 undoPolyBuf)
			(= undoPolyBuf 0)
		)
		(DrawStatus {_} 0 0)
		(DrawStatus 0)
		(gCast eachElementDo: 301)
		(Animate (gCast elements?) 0)
		(self eachElementDo: 80)
		(if
			(Print
				addText: {Erase polygon outlines?}
				addButton: 1 {Yes_} 30 12
				addButton: 0 { No_} 85 12
				init:
			)
			(DrawPic (gRoom curPic?) 100 1)
			(if (!= gPicNumber -1) (DrawPic gPicNumber 100 0))
			(gAddToPics doit:)
		)
		(= gWindow theMainWin)
		(DisposeScript 993)
		(super dispose:)
		(DisposeScript 943)
	)
	
	(method (add)
		(super add: (= curPolygon (_EditablePolygon new:)))
		(return curPolygon)
	)
	
	(method (delete &tmp theCurPolygon)
		(= theCurPolygon curPolygon)
		(self advanceRetreat: 128 65)
		(if (== curPolygon theCurPolygon) (= curPolygon 0))
		(super delete: theCurPolygon &rest)
		(theCurPolygon dispose:)
	)
	
	(method (handleEvent param1 &tmp temp0 theX theY [temp3 20])
		(= theX x)
		(= theY y)
		(= x (param1 x?))
		(= y (param1 y?))
		(switch (param1 type?)
			(0
				(if curPolygon
					(if
						(and
							isMouseDown
							(not (IsOneOf state 0 2))
							(> (+ (Abs (- theX x)) (Abs (- theY y))) 1)
						)
						(if (!= state 3) (self saveForUndo:))
						(self changeState: 2)
					)
					(if
					(and (IsOneOf state 2 0) (or (!= theX x) (!= theY y)))
						(self movePt: x y)
					)
					(if (== state 2)
						(DrawStatus (Format @temp3 943 2 x y))
					)
				)
			)
			(1
				(= temp0 (param1 modifiers?))
				(= isMouseDown 1)
				(cond 
					((& temp0 $0004)
						(if (== state 0)
							(self finishAdding:)
							(= isMouseDown 0)
						else
							(self insertPt:)
						)
					)
					((& temp0 $0003) (if (!= state 0) (self deletePt:)) (= isMouseDown 0))
					((== state 0) (self addPt:))
					(else (self selectPt:))
				)
			)
			(2
				(= isMouseDown 0)
				(if (IsOneOf state 2 3) (self changeState: 1))
			)
			(4
				(switch (param1 message?)
					(63 (param1 message: 104))
					(19 (param1 message: 120))
					(15360 (param1 message: 12032))
					(15872 (param1 message: 11776))
				)
				(switch (param1 message?)
					(9
						(if (and (== state 1) curPolygon)
							(self advanceRetreat: 65 124)
						)
					)
					(3840
						(if (and (== state 1) curPolygon)
							(self advanceRetreat: 128 127)
						)
					)
					(32
						(if (and (== state 1) curPolygon)
							(curPolygon advance:)
						)
					)
					(8
						(if (and (== state 1) curPolygon)
							(curPolygon retreat:)
						)
					)
					(99
						(if (== state 1)
							(self changeState: 0)
							(= curPolygon 0)
						)
					)
					(116
						(if (and curPolygon (== state 1))
							(curPolygon getAccessType:)
						)
					)
					(100
						(cond 
							((== state 1) (if curPolygon (self deletePt:)))
							((== state 0) (self finishAdding:))
						)
					)
					(104
						(switch state
							(0
								(localproc_02d8
									{___________CREATING POLYGON\n\n\n\nClick to create each corner of the polygon, then choose Done from the menu to finish.__You can also press Esc or Ctrl-click to finish.\n\n\n\nTo UNDO a corner, choose Undo.\n\n\n\nTo change MAP displayed (visual or control), choose Map.\n\n\n\nTo EXIT the Polygon Editor, choose eXit or press Ctrl-S.}
									0
								)
							)
							(1
								(localproc_02d8
									{_____________EDITING POLYGON\n\n\n\nTo MOVE a corner, click on it and drag it to the new position.\n\nTo INSERT a new corner, Ctrl-click to create it, then drag it to the correct position.\n\nTo DELETE a corner, Shift-click on it.\n\nTo UNDO an action, choose Undo from the menu.\n\nTo CREATE a new polygon, choose Create.\n\nTo change a polygon's TYPE (Total, Near or Barred), choose Type.\n\nTo change MAP displayed (visual or control), choose Map.\n\nTo EXIT the Polygon Editor, choose eXit or press Ctrl-S.\n\n\n\nIn addition to using the mouse, you can use Space and BackSpace to select corners and Tab and BackTab to select polygons._}
									0
								)
							)
						)
					)
					(117 (self undo:))
					(109 (self showMap: -1))
					(12032 (self showMap: 1))
					(11776 (self showMap: 4))
					(97
						(localproc_02d8
							{ by\n\n\n\nMark Wilden\n\n\n\nOriginal program by Chad Bye_}
							1
						)
					)
					(114
						(if (== state 1) (self draw:))
					)
					(120 (return (self exit:)))
					(27
						(if (== state 0) (self finishAdding:))
					)
				)
			)
		)
		(return 0)
	)
	
	(method (changeState theState)
		(if curMenu (curMenu dispose:))
		(= curMenu
			(switch (= state theState)
				(0 addMenu)
				(1 editMenu)
				(2 0)
				(else  0)
			)
		)
		(if curMenu (curMenu init:))
	)
	
	(method (draw)
		(self eachElementDo: 76)
		(self eachElementDo: 75)
		(self eachElementDo: 80)
		(Graph 12 0 0 190 320 local0)
	)
	
	(method (select param1 param2 &tmp temp0 theCurPolygon temp2 theTheCurPolygon temp4)
		(= temp0 32767)
		(= theCurPolygon 0)
		(= temp4 (FirstNode elements))
		(while temp4
			(= theTheCurPolygon (NodeValue temp4))
			(= temp2 (theTheCurPolygon param1: x y))
			(if (< temp2 temp0)
				(= temp0 temp2)
				(= theCurPolygon theTheCurPolygon)
			)
			(= temp4 (NextNode temp4))
		)
		((= curPolygon theCurPolygon) setCurClosest: param2)
	)
	
	(method (selectPt &tmp newEvent)
		(self select: 483 1)
		(= newEvent (Event new:))
		(GlobalToLocal newEvent)
		(= x (newEvent x?))
		(= y (newEvent y?))
		(newEvent dispose:)
	)
	
	(method (addPt)
		(self saveForUndo:)
		(if (not curPolygon)
			(self add:)
			(curPolygon add: x y 0)
		)
		(curPolygon add: x y)
	)
	
	(method (finishAdding &tmp polyEditFirst)
		(if curPolygon
			(curPolygon closed: 1)
			(if (> (curPolygon size?) 1)
				(curPolygon deletePt: (curPolygon last:) advance:)
			)
			(self draw:)
			(curPolygon getAccessType:)
		else
			(= polyEditFirst (self first:))
			(if (not polyEditFirst)
				(= curPolygon 0)
			else
				(= curPolygon (NodeValue polyEditFirst))
				(self draw:)
			)
		)
		(if curPolygon (self changeState: 1))
	)
	
	(method (movePt param1 param2)
		(curPolygon startRedraw:)
		(self eachElementDo: 76)
		(curPolygon movePt: param1 param2)
		(self eachElementDo: 75)
		(self eachElementDo: 80)
		(curPolygon endRedraw:)
	)
	
	(method (insertPt)
		(self eachElementDo: 76)
		(self select: 482 0)
		(self saveForUndo:)
		(curPolygon insertPt: x y)
		(self changeState: 3)
		(self eachElementDo: 75)
		(self eachElementDo: 80)
		(Graph 12 0 0 190 320 local0)
	)
	
	(method (deletePt &tmp temp0)
		(self eachElementDo: 76)
		(self select: 483 0)
		(self saveForUndo:)
		(curPolygon deletePt:)
		(if (not (curPolygon size?))
			(self delete: curPolygon)
			(if (not size) (self changeState: 0))
		)
		(self eachElementDo: 75)
		(self eachElementDo: 80)
		(Graph 12 0 0 190 320 local0)
	)
	
	(method (undo &tmp temp0 theUndoPoly theUndoPrvPoly theUndoPolyBuf theUndoX theUndoY theUndoState)
		(if (not undoPoly) (Prints {Nothing to undo}) (return))
		(= theUndoPoly undoPoly)
		(= theUndoPrvPoly undoPrvPoly)
		(= theUndoPolyBuf undoPolyBuf)
		(= theUndoX undoX)
		(= theUndoY undoY)
		(= theUndoState undoState)
		(self saveForUndo: 0)
		(self eachElementDo: 76)
		(if (= curPolygon theUndoPoly)
			(if (not (self contains: curPolygon))
				(= curPolygon (self add:))
				(if theUndoPrvPoly
					(self addAfter: theUndoPrvPoly curPolygon)
				else
					(self addToFront: curPolygon)
				)
			)
			(curPolygon undo: theUndoPolyBuf)
		else
			(= curPolygon (self add:))
		)
		(Memory 3 theUndoPolyBuf)
		(= x theUndoX)
		(= y theUndoY)
		(self changeState: theUndoState)
		(self eachElementDo: 75)
		(self eachElementDo: 80)
		(Graph 12 0 0 190 320 local0)
		(gGame setCursor: 999 1 x y)
	)
	
	(method (saveForUndo param1 &tmp [temp0 2])
		(if (= undoPoly curPolygon)
			(= undoPrvPoly (self prev: (= undoPoly curPolygon)))
			(if (and (or (not argc) param1) undoPolyBuf)
				(Memory 3 undoPolyBuf)
			)
			(= undoPolyBuf (curPolygon saveForUndo:))
		)
		(= undoX x)
		(= undoY y)
		(= undoState state)
	)
	
	(method (advanceRetreat param1 param2 &tmp temp0 temp1)
		(= temp1 (FindKey elements curPolygon))
		(= temp0 (self param1: temp1))
		(if
		(and (not temp0) (not (= temp0 (self param2: temp1))))
			(= temp0 temp1)
		)
		(= curPolygon (NodeValue temp0))
		(curPolygon setCur: (curPolygon curNode?))
	)
	
	(method (readObstacles)
		(if (gRoom obstacles?)
			((gRoom obstacles?) eachElementDo: 96 readObstacle 0)
		)
		(if gAltPolyList
			(gAltPolyList eachElementDo: 96 readObstacle 1)
		)
	)
	
	(method (writeObstacles)
		(if (gRoom obstacles?)
			((gRoom obstacles?) eachElementDo: 111 release:)
		)
		(self eachElementDo: 487)
	)
	
	(method (showMap param1)
		(if (== param1 -1)
			(if (== local0 1) (= param1 4) else (= param1 1))
		)
		(if (!= local0 param1)
			(self eachElementDo: 76)
			(= local0 param1)
			(self eachElementDo: 75)
			(self eachElementDo: 80)
			(Graph 12 0 0 190 320 local0)
		)
	)
	
	(method (exit &tmp [temp0 100] temp100 newFile temp102)
		(if (== state 0) (self finishAdding:))
		(self showMap: 1)
		(if (not curPolygon) (return 1))
		(if (not local2)
			(Format @local2 943 3 (gRoom curPic?))
		)
		(= temp100
			(Print
				addTitle: {Save Polygons}
				addText: {File:}
				addEdit: @local2 25 50 0 @local2
				addButton: 1 { Save_} 5 12
				addButton: 2 {Abandon} 70 12
				addButton: 0 {Cancel} 150 12
				init:
			)
		)
		(if (not temp100) (return 0))
		(if (== temp100 2) (return 1))
		(if (FileIO 10 @local2)
			(Format @temp0 943 4 @local2)
			(= temp100
				(Print
					width: 210
					addText: @temp0
					addButton: 1 {Replace} 5 12
					addButton: 2 {Append} 85 12
					addButton: 0 {Cancel} 150 12
					init:
				)
			)
			(if (not temp100) (return 0))
		)
		(= temp102 (if (== temp100 1) 2 else 0))
		(= newFile (File new:))
		(if (not (newFile name: @local2 open: temp102))
			(Format @temp0 943 5 (newFile name?))
			(Prints @temp0)
			(newFile dispose:)
			(return 0)
		)
		(newFile
			writeString: (Format @temp0 943 6 {Polygon Editor 1.11})
		)
		(newFile
			writeString: (Format @temp0 943 7 {Dynamic Obstacles} (gRoom curPic?))
		)
		(newFile writeString: {\t\t(curRoom addObstacle:\n\n})
		(self eachElementDo: 488 newFile 0)
		(newFile writeString: {\t\t)\n\n\n\n})
		(newFile writeString: {\t\t(altPolyList add:\n\n})
		(self eachElementDo: 488 newFile 1)
		(newFile writeString: {\t\t)\n\n})
		(newFile dispose:)
		(return 1)
	)
)

(instance editMenu of ClickMenu
	(properties)
	
	(method (init)
		(super init: @local46)
	)
)

(instance addMenu of ClickMenu
	(properties)
	
	(method (init)
		(super init: @local71)
	)
)

(instance readObstacle of Code
	(properties)
	
	(method (doit param1 param2 &tmp temp0 temp1 polyEditAdd)
		(= polyEditAdd (PolyEdit add:))
		(= temp0 0)
		(= temp1 (param1 points?))
		(while (< temp0 (param1 size?))
			(polyEditAdd
				add: (Memory 5 temp1) (Memory 5 (+ temp1 2)) 0
				type: (param1 type?)
				srcList: param2
			)
			(++ temp0)
			(= temp1 (+ temp1 4))
		)
		(polyEditAdd closed: 1)
	)
)
