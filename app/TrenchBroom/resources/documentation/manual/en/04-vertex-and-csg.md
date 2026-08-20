# Vertex Editing and CSG {#vertex_and_csg}

## Vertex Editing {#vertex_editing}

TrenchBroom includes three separate tools to edit a brush's geometry: the [vertex tool](#vertex_tool) for editing individual vertices, the [edge tool](#edge_tool) for editing individual edges, and the [face tool](#face_tool) for editing individual faces. The vertex tool is the most powerful of the three because, in addition to moving vertices, it can add and remove them. By contrast, the edge and face tools move existing edges or faces without adding or removing vertices.

#### Vertex Tool {#vertex_tool}

Using the vertex tool, you can move individual vertices around in 3D space. Additionally, you can add vertices to a brush, and you can remove vertices from a brush. To activate the vertex tool, choose #menu(Menu/Edit/Tools/Vertex Tool). When the vertex tool is active, yellow handles appear at the vertices of the selected brushes to allow manipulation.

![Vertex Handles](images/VertexToolHandles.png)

Moving the mouse pointer over a vertex handle highlights that handle with a red circular outline, and the position of that handle is displayed above it.

Selecting vertex handles is treated in the same way as selecting objects. Click on a handle to select it. Multiple handles can be selected by holding #key(Ctrl). The vertex tool also allows you to select multiple handles using a selection lasso. Left drag with the mouse button to create a rectangular selection lasso. Release the left mouse button, and all handles inside the lasso are selected. If the lasso rectangle contains a vertex handle that's already selected, then it will be deselected. To ensure that all vertex handles inside the lasso are selected regardless of their previous selection state, hold #key(Ctrl).

When you have selected some vertex handles, you can move them around by dragging them with the left mouse button. Moving vertex handles (and their corresponding vertices) works in a similar fashion to moving objects, so in the 3D viewport, you can move them on the XY plane, or you can hold #key(Alt) to move them vertically. In a 2D viewport, you can move the vertex handles on the view plane of that viewport. If you begin your drag on an unselected vertex handle, that vertex handle is automatically selected, so if you just want to move a single vertex around, you do not need to select it first. Once you press the left mouse button on a vertex handle to begin a drag, yellow guide lines show up that help you to position the vertex in relation to other objects. When moving vertex handles around, the move distances are snapped to the current grid size component wise, just like when you move objects. If you prefer to have the absolute vertex positions snapped to the grid, you can toggle between relative and absolute snapping using #key(Ctrl) during the drag.

![Chopping faces](images/VertexToolFaceChopping.gif) TrenchBroom ensures that you do not create invalid brushes with the vertex tool. For example, it is impossible to make a brush concave by pushing a vertex into the brush. To achieve this, TrenchBroom will chop up the faces incident to that vertex into triangles depending on the direction in which that vertex is moved. In the animation on the left, you can see that the top face of the cube has one triangle chopped off in the first move where the vertex is moved downward, while in the second move, the front face is chopped into a triangle fan when the vertex is moved outward. Sometimes, TrenchBroom will even delete a vertex if a move would push it inside the brush, which would make it concave. In such a case, the vertex move is concluded.

The vertex tool also allows you to fuse adjacent vertices. If a vertex ends up on an adjacent vertex during a vertex move, the two vertices will be fused. This does not conclude the move however, you can keep moving the fused vertex, and it remains selected. Note that fusing is not allowed when you are moving edges or faces, even though fusing does happen when you move multiple vertices at once.

If you wish to snap a vertex onto another vertex quickly without performing a drag, you can just click on the target vertex while holding #key(Shift)#key(Alt).

<br clear="all" />

![Splitting](images/VertexToolSplitting.gif) Besides moving, fusing and deleting vertices, you can also add new vertices to a brush with the vertex tool. Hold #key(Shift) and move your mouse over the position on the grid where you wish to add a vertex. Notice that a new vertex handle is shown when your mouse pointer is close to that point. Click and drag the handle to add a new vertex.

Additionally, you can delete the selected vertices, edges, and faces from brushes by choosing #menu(Menu/Edit/Delete). Note that this will only succeed if none of the currently selected brushes becomes invalid by the deletions, that is, you can only delete vertices, edges, or faces if all of the selected brushes remain three-dimensional after the deletions. If that is not the case, TrenchBroom will refuse the entire operation.

<br clear="all" />

![Vertex clumping](images/VertexToolVertexClumping.gif) Vertex editing is not limited to working with single brushes. Selecting more than one brush and activating the vertex tool will cause vertex handles to appear for all brushes in the selection. This is more useful when working on organic brushwork such as terrain. You can build a large group of brushes and modify them all at once without having to change the selection. TrenchBroom will recognize when vertices of multiple brushes share the same position. In this case, when trying to move a vertex, TrenchBroom will move all those vertices together, making editing terrain much quicker and easier. In the following animation, the vertices under the cursor were moved with a single drag operation because they share the same position.

<br clear="all" />

The vertex tool uses the same keyboard shortcuts as [moving objects](#moving_objects). Each key press moves the selected vertices by the current grid size in the corresponding 2D or 3D direction.

In addition to moving vertices, the vertex tool can also rotate, scale and shear vertices. To rotate vertices, bring up the [rotate tool](#rotating_objects) via #menu(Menu/Edit/Tools/Rotate Tool), then use the rotation handle as usual. To scale them, use #menu(Menu/Edit/Tools/Scale Tool) to enable the [scale tool](#scaling_objects), and use #menu(Menu/Edit/Tools/Shear Tool) to shear them with the [shear tool](#shearing_objects).

This concludes the functionality of the vertex tool. While it is very powerful, it should also be used with care, as vertex editing can sometimes create invalid brushes and microleaks in the map. To help you avoid such problems, the following section contains a few best practices you should keep in mind when you use the vertex tool.


#### Edge Tool {#edge_tool}

Using the edge tool, you can move individual edges around in 3D space. To activate the edge tool, choose #menu(Menu/Edit/Tools/Edge Tool). When the edge tool is active, the edges of the selected brushes are rendered yellow to indicate that they allow manipulation. Furthermore, a handle appears at the center of each edge.

![Edge Handles](images/EdgeTool.png)

Moving the mouse pointer over an edge handle highlights that handle in red. Selecting edge handles works in the same way as selecting vertex handles. Click on a handle to select it. Multiple handles can be selected by holding #key(Ctrl). Selected handles are rendered in red. The edge tool also allows you to select multiple handles using a selection lasso. Left drag with the mouse button to create a rectangular selection lasso. Release the left mouse button, and all handles contained in the lasso are selected. If the lasso rectangle contains an edge handle that's already selected, then it will be deselected. To ensure that all edge handles contained in the lasso are selected regardless of their previous selection state, hold #key(Ctrl).

When you have selected some edge handles, you can move them around by dragging them with the left mouse button. Moving edge handles (and their corresponding edges) works in same way as moving vertices, with the exception that the tool only supports relative snapping of the move distances. Like the vertex tool, the edge tool will detect if two or more brushes share an edge and move the edges of all selected brushes if a shared edge is moved.

Finally, the edge tool also supports the same keyboard commands as the vertex tool.

#### Face Tool {#face_tool}

Using the face tool, you can move individual face around in 3D space. To activate the face tool, choose Choose #menu(Menu/Edit/Tools/Face Tool). When the face tool is active, the faces of the selected brushes are rendered yellow to indicate that they allow manipulation. Additionally, the tool shows point handles at the centers of the faces; these handles allow selection and manipulation of the faces. Note that the handles allow you to select and manipulate faces which face away from the camera, too.

![Face Handles](images/FaceTool.png)

Moving the mouse pointer over an face handle highlights that handle with a red outline. Additionally, the face edges are rendered thicker. Selecting face handles works in the same way as selecting vertex handles. Click on a handle to select it. Multiple handles can be selected by holding #key(Ctrl). Selected handles are rendered in red. The face tool also allows you to select multiple faces using a selection lasso. Left drag with the mouse button to create a rectangular selection lasso. Release the left mouse button, and all face handles contained in the lasso are selected. If the lasso rectangle contains a face handle that's already selected, then it will be deselected. To ensure that all face handles contained in the lasso are selected regardless of their previous selection state, hold #key(Ctrl).

When you have selected some face handles, you can move them around by dragging them with the left mouse button. Moving face handles (and their corresponding faces) works in same way as moving vertices, with the exception that the tool only supports relative snapping of the move distances. Like the vertex tool, the face tool will detect if two or more brushes share an face and move the faces of all selected brushes if a shared face is moved.

Finally, the face tool also supports the same keyboard commands as the vertex tool.

#### Vertex Editing Best Practices {#vertex-editing-best-practices}

- Don't use it too much on sealing brushes, better to use it on detail.
- Don't do too much in one go, compile and test often.
- When doing terrain, create a quad- or trisoup and only move vertices orthogonal to the grid's plane (along the plane's normal). Don't move vertices sideways.
- Be careful with detail brushes, they might open vis portals.
- Detail might also result in PVS leaves too much information, better to turn some detail into actual brushes to force vis to break a room into several PVS leaves. Or use hint brushes to force vis to add more leaves.

#### UV Lock {#uv_lock}

The regular Alignment Lock preference doesn't apply to vertex editing - instead, there is a separate preference called UV Lock toggled with #menu(Menu/Edit/UV Lock) or the UV Lock toolbar button:

![UV Lock toolbar button](images/UVLock.png)

When this setting is enabled, TrenchBroom will attempt to keep vertex UV coordinates the same when using the vertex editing tools or [face moving](#moving_faces).

## CSG Operations {#csg-operations}

CSG stands for Constructive Solid Geometry. CSG is a technique used in professional modeling tools to create complex shape by combining simple shapes using set operators such as union (sometimes called addition), subtraction, and intersection. However, CSG union and subtraction cannot be directly applied to brushes since they may create concave shapes which cannot be represented directly using brushes (remember that brushes are always convex). But some of these operators can be emulated with brushes. TrenchBroom supports the operations _convex merge_ (in place of union), _subtraction_ (emulated by creating new brushes to represent the resulting concave shape), and _intersection_ (supported directly).

#### CSG Convex Merge {#csg-convex-merge}

Convex merge takes a set of brushes as its input, computes the _convex hull_ of all vertices of those brushes, and creates a new brush with the shape of the convex hull. The convex hull of a set of points is the smallest convex volume that contains all of the points. In the following animation, two brushes are being merged into one. The operation takes the vertices of both brushes and computes its convex hull. Some of the original brushes' vertices end up as vertices of the convex hull, and some of them are discarded, such as the vertices at the bottom right corner of the top left brush in the 2D viewport. The discarded vertices are those which end up inside the convex hull.

![CSG convex merge](images/CSGConvexMerge.gif)

As you can see, the newly created brush covers some areas which were not covered by the original brushes. This follows the restriction that the resulting brush must be convex. Whether the resulting brush covers such previously void areas depends on how the input brushes are aligned with each other. To perform a convex merge, select the brushes to be merged and choose #menu(Menu/Edit/CSG/Convex Merge).

#### CSG Subtraction {#csg-subtraction}

CSG subtraction takes the selected brushes (the subtrahend) and subtracts them the rest of the selectable, visible brushes in the map (the minuend). Since the result of a CSG subtraction is potentially concave, TrenchBroom creates brushes that represent the concave shape by cutting up the minuend brushes using the faces of the subtrahend brushes.

![CSG subtracting to create an arch](images/CSGSubtractArch.gif)

The image above shows an example where an arch is created by subtraction. The result contains eight brushes that perfectly represent the arch. To perform a CSG subtraction, select the subtrahends (the brushes you want subtracted from the world) and choose #menu(Menu/Edit/CSG/Subtract).

To exclude brushes from the subtraction, you can hide them first with #menu(Menu/View/Hide).

#### CSG Hollow {#csg-hollow}


CSG hollow is a shortcut for subtracting a smaller version of a brush from itself. This can be useful to quickly block out rooms. Just select a brush and choose #menu(Menu/Edit/CSG/Hollow) to hollow out the selected brush. The resulting walls will have a thickness of the current grid size.

![CSG hollow](images/CSGHollow.gif)

In this example, a cube is hollowed out, leaving six brushes which form the walls, floor, and ceiling of the resulting room. Note that the wall thickness is determined by the grid size.

#### CSG Intersection {#csg-intersection}

CSG intersection takes a set of brushes and computes their intersection, that is, it computes the largest brush that is contained in each of the input brushes. Another way to think of this is that intersection takes that part of the input brushes where they all overlap, and creates a new brush that represents that part. If there is no such part, then the input brushes are disjoint, and their intersection is empty. The input brushes are then removed from the map.

![CSG intersection of two cuboids](images/CSGIntersect.gif)

You can perform a CSG intersection by selecting the brushes you wish to intersect, and then choosing #menu(Menu/Edit/CSG/Intersect).

#### Materials and CSG Operations {#materials_and_csg_operations}

In each of the CSG operations, new brushes are created, and TrenchBroom has to assign materials to their faces. To determine which material to assign to a new brush face, TrenchBroom will attempt to find a face in the input brushes that has the same plane as the newly created face. If such a face was found, TrenchBroom assigns the material and attributes of that brush face to the newly created brush face. Otherwise, it will assign the [current material](#working_with_materials).

![CSG material handling](images/CSGTexturing.gif)

In the example above, a brush is subtracted from another brush to form an archway. The subtrahend has a blue brick material and the minuend has a dark metal material. After the subtraction, those faces of the result that line up with the faces of the subtrahend have also been assigned the blue brick material, while those faces that line up with the faces of the minuend brush have been assigned the dark metal material. Some of the faces of the result brushes are invisible because they are shared by other brushes - these faces have been assigned the current material because no face of the subtrahend or the minuend lines up with them.

## Special Brush and Face Types {#special_brush_face_types}

Most Quake based games have certain special types of brushes and faces. An example for a special brush type is a *trigger brush* that activates some game logic. A special face type may be a face with a special _clip_ material that is invisible, but blocks the player from passing it. In Quake 3, _caulk_ faces are often used to tell the bsp compiler to skip certain faces because they are invisible.

TrenchBroom is aware of these special brush and face types as long as they are configured in the [game configuration file](#game_configuration_files) for a game. Special brush or face types can be detected by TrenchBroom in the following ways:

* **Brush Types**
  - **Entity Classname Pattern** If a brush is contained in an entity whose classname matches a certain pattern, it will receive the special brush type (example: trigger brushes).
* **Face Types**
  - **Material Name Pattern** If a brush face has a material whose name matches a certain pattern, it will receive the special face type (example: clip textures).
  - **Content Flags** If a brush has one of several content flags, it will receive the special face type.
  - **Surface Flags** If a brush has one of several surface flags, it will receive the special face type.
  - **Surface Parm** If a brush has a specific surface parameter, it will receive the special face type.

TrenchBroom allows you to filter for special brush and face types in the [filtering and rendering options](#filtering_rendering_options). Furthermore, you can assign a keyboard shortcut to set or unset a special brush type (if supported). The following special brush and face types can be set / unset in this way:

* **Brush Types**
  - **Entity Classname Pattern** Can be set and unset. Setting a brush type wraps the selected brushes in a newly created entity with a classname that matches the pattern. If more than one classnames in the currently loaded entity definitions matches the pattern, a dropdown menu is shown to allow you to choose one of the options. Unsetting a brush type simply moves the selected brushes into the world.
* **Face Types**
  - **Material Name Pattern** Can be set only. Setting a face type sets a material whose name matches the pattern on the selected faces. If more than one material in the currently loaded material collections matches the pattern, a dropdown menu is shown to allow you to choose one of the options.
  - **Content Flags** Can be set and unset. Setting a face type sets one of the configured content flags on the selected faces. If more than one content flag was configured, a dropdown menu is shown to allow you to choose one of the options. Unsetting clears all of the configured content flags.
  - **Surface Flags** Can be set and unset. Setting a face type sets one of the configured surface flags on the selected faces. If more than one surface flag was configured, a dropdown menu is shown to allow you to choose one of the options. Unsetting clears all of the configured surface flags.
  - **Surface Parm** Can neither be set nor unset.

Finally, every special brush or face type that supports unsetting can be cleared using the *Make Structural* command #action(Controls/Map view/Make structural).
