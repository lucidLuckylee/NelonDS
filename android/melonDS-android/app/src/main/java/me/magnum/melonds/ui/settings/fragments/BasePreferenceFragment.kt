package me.magnum.melonds.ui.settings.fragments

import android.animation.AnimatorInflater
import android.graphics.Rect
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.preference.PreferenceCategory
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceGroupAdapter
import androidx.preference.PreferenceScreen
import androidx.preference.PreferenceViewHolder
import androidx.recyclerview.widget.RecyclerView
import me.magnum.melonds.R

abstract class BasePreferenceFragment : PreferenceFragmentCompat() {
    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()

    override fun onCreateAdapter(preferenceScreen: PreferenceScreen): RecyclerView.Adapter<*> =
        object : PreferenceGroupAdapter(preferenceScreen) {
            override fun onBindViewHolder(holder: PreferenceViewHolder, position: Int) {
                super.onBindViewHolder(holder, position)
                val isCategory = getItem(position) is PreferenceCategory
                holder.itemView.apply {
                    if (isCategory) {
                        background = null
                        elevation = 0f
                        stateListAnimator = null
                    } else {
                        setBackgroundResource(R.drawable.settings_card)
                        elevation = dp(3).toFloat()
                        minimumHeight = dp(76)
                        stateListAnimator = AnimatorInflater.loadStateListAnimator(context, R.animator.settings_card_lift)
                    }
                }
                holder.isDividerAllowedAbove = false
                holder.isDividerAllowedBelow = false
            }
        }

    override fun onCreateRecyclerView(inflater: LayoutInflater, parent: ViewGroup, savedInstanceState: Bundle?): RecyclerView {
        return super.onCreateRecyclerView(inflater, parent, savedInstanceState).apply {
            clipToPadding = false
            val horizontalPadding = dp(if (resources.configuration.screenWidthDp >= 600) 40 else 20)
            setPadding(horizontalPadding, dp(8), horizontalPadding, dp(20))
            addItemDecoration(object : RecyclerView.ItemDecoration() {
                override fun getItemOffsets(outRect: Rect, view: View, parent: RecyclerView, state: RecyclerView.State) {
                    outRect.bottom = dp(10)
                }
            })
            ViewCompat.setOnApplyWindowInsetsListener(this) { view, windowInsets ->
                val insets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
                view.updatePadding(bottom = insets.bottom + dp(20))
                WindowInsetsCompat.CONSUMED
            }
        }
    }
}
