# UE4-MultiCollisionMovement


Project example for UE 4.18 

Movement for character with complex collision shape
When you need to have a character with complex collision shape like a vehicle or a space ship or something similar - you will have character capsule size be too much bigger than needed. This will come to some restriction in movement of the character – character will not be able to come close to objects, move in narrow places and etc.
The next solution is not a complete solution but just a one of the ways and example and can be used as entry point you can start from. It was used for flying objects mainly, but it also support other physics modes but I think other additional code changes will be required additionally.



How to setup:
1.	Character MC should have one of the following settings enabled:
bOrientRotationToMovement or bUseControllerDesiredRotation
2.	Change root collision size:
•	For flying physics you can drop root capsule size to unit and disable root capsule collision at all. 
•	For walking physics root capsule size should be based on main walkable mesh

3.	You add MultiCollisionCapsuleComponent to cover the mesh shape and you build with this additional components complex shape of the character.
How it works:
1.	At character initialization movement component calls collect and update all MultiCollisionCapsuleComponent added to the character
2.	At movement phase instead of moving and checking root capsule component – movement component simulates movement with sweeps on all additional components and checks the most first hit and stops on it not allowing to move far than possible.
3.	The good move result is applied to root component as normal movement.
4.	Rotation is also physics based and happens after movement is finished, when character turns – all collisions are also tested and checked.
Optimization:
1.	Many additional components can drop FPS when you have many characters which are moving/colliding/sliding at same time. It is important to have the quantity of additional components as less as possible.
2.	Movement and Turning happens in different passes but it could be combined to one pass to decrease sweep checks.
For example this could be done with next changes:
Disabling PhysicsRotation function and enabling Character FaceRotation function which should calc new rotation for current frame. Then you need to override physics mode function so it will pass your new calculated rotation to movement code.

This system can be used with modular characters with some changes.



