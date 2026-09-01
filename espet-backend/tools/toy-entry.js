/* ESPET toy-agent 3D viewer.
   Bundles three.js + the RobotExpressive glTF (CC0, Tomás Laulhé) into an
   IIFE exposing `ToyAgent`:

     ToyAgent.mount(el)            – create renderer/scene, load robot.glb
     ToyAgent.setOpts(opts)        – { bodyColor, headColor, hat, hatColor,
                                       glasses, antenna, bowtie, mood }
     ToyAgent.play(name, once)     – "Dance" | "Wave" | "Jump" | "ThumbsUp" | "Idle"
     ToyAgent.unmount()            – dispose

   Drag to rotate; auto-rotates slowly when left alone. */

import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";

const MODEL_URL = "/static/vendor/models/robot.glb";

let ctx = null;

/* ── Prop meshes (primitives, glued to bones) ─────────────────── */

function stdMat(color, metal = 0.1, rough = 0.55) {
  return new THREE.MeshStandardMaterial({ color, metalness: metal, roughness: rough });
}

function buildHat(type, color) {
  if (!type || type === "none") return null;
  const g = new THREE.Group();
  const mat = stdMat(color || 0x262e33);
  if (type === "tophat") {
    const brim = new THREE.Mesh(new THREE.CylinderGeometry(0.34, 0.34, 0.04, 24), mat);
    const tube = new THREE.Mesh(new THREE.CylinderGeometry(0.22, 0.23, 0.34, 24), mat);
    tube.position.y = 0.19;
    g.add(brim, tube);
    g.userData.y = 1.38;
  } else if (type === "party") {
    const cone = new THREE.Mesh(new THREE.ConeGeometry(0.17, 0.42, 20), mat);
    cone.position.y = 0.21;
    const pom = new THREE.Mesh(new THREE.SphereGeometry(0.055, 12, 12), stdMat(0xf1f5f9));
    pom.position.y = 0.44;
    g.add(cone, pom);
    g.userData.y = 1.32;
    g.rotation.z = 0.12;
  } else if (type === "propeller") {
    const cap = new THREE.Mesh(new THREE.SphereGeometry(0.26, 20, 12, 0, Math.PI * 2, 0, Math.PI / 2), mat);
    const stem = new THREE.Mesh(new THREE.CylinderGeometry(0.02, 0.02, 0.1, 8), stdMat(0xf1f5f9));
    stem.position.y = 0.26;
    const blades = new THREE.Group();
    const bladeMat = stdMat(0xf59e0b);
    for (let i = 0; i < 2; i++) {
      const b = new THREE.Mesh(new THREE.BoxGeometry(0.42, 0.02, 0.07), bladeMat);
      b.rotation.y = (i * Math.PI) / 2;
      blades.add(b);
    }
    blades.position.y = 0.33;
    blades.name = "propeller";
    g.add(cap, stem, blades);
    g.userData.y = 1.30;
  } else if (type === "beanie") {
    const cap = new THREE.Mesh(new THREE.SphereGeometry(0.27, 20, 12, 0, Math.PI * 2, 0, Math.PI / 2), mat);
    const band = new THREE.Mesh(new THREE.TorusGeometry(0.25, 0.045, 10, 24), stdMat(0xf1f5f9));
    band.rotation.x = Math.PI / 2;
    band.position.y = 0.02;
    g.add(cap, band);
    g.userData.y = 1.26;
  }
  return g;
}

function buildGlasses(type) {
  if (!type || type === "none") return null;
  const g = new THREE.Group();
  if (type === "round") {
    const rim = stdMat(0x0f172a, 0.3, 0.4);
    for (const x of [-0.13, 0.13]) {
      const t = new THREE.Mesh(new THREE.TorusGeometry(0.095, 0.022, 10, 24), rim);
      t.position.set(x, 0.06, 0.02);
      const lens = new THREE.Mesh(new THREE.CircleGeometry(0.088, 20),
        new THREE.MeshStandardMaterial({ color: 0x93c5fd, transparent: true, opacity: 0.35 }));
      lens.position.set(x, 0.06, 0.02);
      g.add(t, lens);
    }
    const bridge = new THREE.Mesh(new THREE.BoxGeometry(0.09, 0.02, 0.02), rim);
    bridge.position.set(0, 0.06, 0.02);
    g.add(bridge);
    g.userData.y = 0.16;
    g.userData.z = 0.3;
  } else if (type === "visor") {
    const v = new THREE.Mesh(
      new THREE.BoxGeometry(0.4, 0.13, 0.06),
      new THREE.MeshStandardMaterial({ color: 0x10b981, metalness: 0.5, roughness: 0.25 })
    );
    v.position.y = 0.06;
    g.add(v);
    g.userData.y = 0.16;
    g.userData.z = 0.3;
  }
  return g;
}

function buildAntenna(type, color) {
  if (!type || type === "none") return null;
  const g = new THREE.Group();
  const stem = new THREE.Mesh(new THREE.CylinderGeometry(0.03, 0.03, 0.5, 8), stdMat(0x94a3b8, 0.4, 0.4));
  stem.position.y = 0.25;
  const ball = new THREE.Mesh(new THREE.SphereGeometry(0.13, 14, 14), stdMat(color || 0x10b981, 0.35, 0.3));
  ball.position.y = 0.55;
  g.add(stem, ball);
  g.userData.y = 1.3;
  return g;
}

function buildBowtie(color) {
  const g = new THREE.Group();
  const mat = stdMat(color || 0xff6b6b);
  for (const s of [-1, 1]) {
    const wing = new THREE.Mesh(new THREE.BoxGeometry(0.11, 0.07, 0.04), mat);
    wing.position.x = s * 0.075;
    wing.rotation.z = s * 0.25;
    g.add(wing);
  }
  const knot = new THREE.Mesh(new THREE.BoxGeometry(0.045, 0.045, 0.045), stdMat(0x0f172a));
  g.add(knot);
  return g;
}

/* ── Scene lifecycle ──────────────────────────────────────────── */

export function unmount() {
  if (!ctx) return;
  ctx.renderer.setAnimationLoop(null);
  ctx.observer?.disconnect();
  ctx.renderer.dispose();
  ctx.el.innerHTML = "";
  ctx = null;
}

export function setOpts(opts = {}) {
  if (!ctx) { pendingOpts = opts; return; }
  const c = ctx;
  if (!c.ready) { pendingOpts = opts; return; } // model still loading

  /* colors */
  if (opts.bodyColor && c.matMain) c.matMain.color.set("#" + opts.bodyColor);
  if (opts.headColor && c.matHeadMain) c.matHeadMain.color.set("#" + opts.headColor);

  /* props: rebuild all, attach to Head / Neck bones */
  for (const p of c.props) p.parent?.remove(p);
  c.props = [];

  const hat = buildHat(opts.hat, opts.hatColor ? parseInt(opts.hatColor, 16) : undefined);
  hat?.scale.setScalar(1.5);
  const glasses = buildGlasses(opts.glasses);
  const antenna = buildAntenna(opts.antenna, opts.antennaColor ? parseInt(opts.antennaColor, 16) : undefined);
  antenna?.scale.setScalar(1.4);
  const bowtie = opts.bowtie && opts.bowtie !== "none"
    ? buildBowtie(opts.bowtieColor ? parseInt(opts.bowtieColor, 16) : 0xff6b6b) : null;

  const attach = (prop, bone, y, z) => {
    if (!prop || !bone) return;
    /* rig bones carry a 100x world scale (the skin compensates via bind
       matrices, raw children don't) — offsets/sizes are WORLD units */
    const s = bone.getWorldScale(new THREE.Vector3()).x || 1;
    prop.position.y = (prop.userData.y ?? y) / s;
    if (prop.userData.z !== undefined) prop.position.z = prop.userData.z / s;
    else if (z !== undefined) prop.position.z = z / s;
    prop.scale.divideScalar(s);
    bone.add(prop);
    c.props.push(prop);
  };

  attach(hat, c.headBone, 1.38, 0);
  attach(glasses, c.headBone, 0.16, 0.3);
  attach(antenna, c.headBone, 1.40, 0);
  if (bowtie && c.neckBone) attach(bowtie, c.neckBone, 0, 0.26);

  /* mood animation */
  if (opts.mood && c.actions[opts.mood]) fadeTo(opts.mood, false);
}

function fadeTo(name, once = false) {
  const c = ctx;
  const next = c.actions?.[name];
  if (!next) return;
  if (next === c.current && !once) return;
  next.reset();
  next.setLoop(once ? THREE.LoopOnce : THREE.LoopRepeat, Infinity);
  next.clampWhenFinished = once;
  if (c.current?.isRunning?.()) next.crossFadeFrom(c.current, 0.35, true);
  else next.crossFadeFrom(c.actions.Idle, 0.35, true);
  next.play();
  c.current = next;
  if (once) {
    const onFinish = (e) => {
      if (e.action !== next) return;
      c.mixer.removeEventListener("finished", onFinish);
      fadeTo("Idle", false);
    };
    c.mixer.addEventListener("finished", onFinish);
  }
}

export function play(name, once = false) {
  if (!ctx || !ctx.ready) return;
  fadeTo(name, once);
}

export async function mount(el, opts = {}) {
  unmount();
  const width = el.clientWidth || 320;
  const height = el.clientHeight || 300;

  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.setSize(width, height);
  el.appendChild(renderer.domElement);
  renderer.domElement.style.touchAction = "pan-y";
  renderer.domElement.style.cursor = "grab";

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(38, width / height, 0.1, 100);
  camera.position.set(0, 1.9, 6.6);
  camera.lookAt(0, 1.55, 0);

  scene.add(new THREE.HemisphereLight(0xffffff, 0x334155, 1.6));
  const key = new THREE.DirectionalLight(0xffffff, 2.4);
  key.position.set(2.5, 4, 3);
  scene.add(key);
  const rim = new THREE.DirectionalLight(0x10b981, 1.1);
  rim.position.set(-3, 2, -2.5);
  scene.add(rim);

  ctx = { el, renderer, scene, camera, props: [], targetRotY: 1.1, lastDrag: 0, matMain: null, matHeadMain: null, headBone: null, neckBone: null, ready: false };

  const gltf = await new GLTFLoader().loadAsync(MODEL_URL);
  if (!ctx) { gltf.dispose?.(); return; } // unmounted while loading
  const robot = gltf.scene;
  scene.add(robot);

  /* adaptive framing: fit the whole model with headroom (zoom>1 = closer) */
  const bbox = new THREE.Box3().setFromObject(robot);
  const modelH = bbox.getSize(new THREE.Vector3()).y;
  const dist = (modelH / 2) / Math.tan(THREE.MathUtils.degToRad(camera.fov / 2)) * 1.32 / (opts.zoom || 1);
  camera.position.set(0, modelH * 0.58, dist);
  camera.lookAt(0, modelH * 0.54, 0);

  /* materials: Main = body, clone for the head so they can differ */
  let matMain = null;
  robot.traverse((o) => {
    if (!o.isMesh || !Array.isArray(o.material)) return;
    o.material = o.material.map((m) => {
      if (m.name === "Main") {
        if (!matMain) matMain = m;
        return o.name === "Head" ? (ctx.matHeadMain = (ctx.matHeadMain || m.clone())) : matMain;
      }
      return m;
    });
    if (o.name === "Head") ctx.matHeadMain = ctx.matHeadMain || null;
  });
  ctx.matMain = matMain;
  ctx.robot = robot;

  ctx.headBone = robot.getObjectByName("Head") || null;
  ctx.neckBone = robot.getObjectByName("Neck") || null;

  /* animations */
  ctx.mixer = new THREE.AnimationMixer(robot);
  ctx.actions = {};
  for (const clip of gltf.animations) {
    ctx.actions[clip.name] = ctx.mixer.clipAction(clip);
  }
  ctx.current = ctx.actions.Idle;
  ctx.current?.play();
  ctx.ready = true;

  /* pointer drag to rotate, gentle auto-rotate when idle */
  let dragging = false, lastX = 0;
  const dom = renderer.domElement;
  dom.addEventListener("pointerdown", (e) => { dragging = true; lastX = e.clientX; dom.style.cursor = "grabbing"; });
  window.addEventListener("pointerup", () => { dragging = false; dom.style.cursor = "grab"; });
  dom.addEventListener("pointermove", (e) => {
    if (!dragging || !ctx) return;
    ctx.targetRotY += (e.clientX - lastX) * 0.012;
    ctx.lastDrag = performance.now();
    lastX = e.clientX;
  });

  ctx.observer = new ResizeObserver(() => {
    if (!ctx) return;
    const w = el.clientWidth || width, h = el.clientHeight || height;
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    renderer.setSize(w, h);
  });
  ctx.observer.observe(el);

  let lastT = performance.now();
  renderer.setAnimationLoop(() => {
    if (!ctx) return;
    const now = performance.now();
    const dt = Math.min((now - lastT) / 1000, 0.1);
    lastT = now;
    const t = now / 1000;
    ctx.mixer?.update(dt);
    if (performance.now() - ctx.lastDrag > 2600) ctx.targetRotY += dt * 0.35;
    if (ctx.robot) {
      ctx.robot.rotation.y += (ctx.targetRotY - ctx.robot.rotation.y) * 0.12;
      ctx.robot.position.y = Math.sin(t * 1.7) * 0.045; // idle float
    }
    const prop = ctx.props.find((p) => p.getObjectByName("propeller"));
    if (prop) prop.getObjectByName("propeller").rotation.y += dt * 12;
    renderer.render(scene, camera);
  });

  /* re-apply any opts set while the model was still loading */
  if (pendingOpts) { const p = pendingOpts; pendingOpts = null; setOpts(p); }
  return ctx;
}

let pendingOpts = null;

export function debug() {
  if (!ctx || !ctx.headBone) return null;
  const v = new THREE.Vector3();
  ctx.headBone.getWorldPosition(v);
  return {
    headLocal: ctx.headBone.position.toArray(),
    headWorld: v.toArray(),
    headScale: ctx.headBone.scale.toArray(),
    headUp: new THREE.Vector3(0, 1, 0).applyQuaternion(ctx.headBone.getWorldQuaternion(new THREE.Quaternion())).toArray(),
    props: ctx.props.map((p) => { const w = p.getWorldPosition(new THREE.Vector3()); return { y: +p.position.y.toFixed(3), world: w.toArray().map(n=>+n.toFixed(2)), parent: p.parent?.name, parentScale: p.parent?.getWorldScale(new THREE.Vector3()).toArray().map(n=>+n.toFixed(3)) }; }),
    modelH: ctx.robot ? new THREE.Box3().setFromObject(ctx.robot).getSize(new THREE.Vector3()).toArray() : null,
  };
}
